"""Local screenshot benchmark. No screenshot is transmitted or used to tune parameters.

Run with the isolated build-contour/model-env Python. Download dependencies and
weights beforehand; see the generated sources.json and README.md for provenance.
"""
import argparse
import ast
import hashlib
import importlib.util
import json
import platform
import time
from pathlib import Path
from typing import List

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OLD = ROOT / 'docs/evidence/contour-2026-10-18'
OUT = ROOT / 'docs/evidence/contour-models-2026-10-18'
MODELS = ROOT / 'build-contour/models'


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def xywh(box):
    x, y, u, v = [round(float(x)) for x in box]
    return [x, y, max(0, u-x), max(0, v-y)]


def queries(case, boxes):
    targets = []
    for target in case['targets']:
        x, y, w, h = target['rect']
        qs = []
        for fx, fy in [(.5,.5),(.25,.25),(.75,.25),(.25,.75),(.75,.75)]:
            # Positive pixel coordinates: match Qt qRound, including .5 ties.
            px, py = int(x+w*fx+.5), int(y+h*fy+.5)
            start = time.perf_counter_ns()
            matched = [b for b in boxes if b[0] <= px < b[0]+b[2] and b[1] <= py < b[1]+b[3]]
            matched.sort(key=lambda b: b[2]*b[3])
            elapsed = (time.perf_counter_ns()-start)/1000
            qs.append({'point':[px,py], 'boxes':matched, 'query_us':elapsed})
        targets.append(dict(target, queries=qs))
    return targets


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('model', choices=['omni','omni-ocr','fastsam-s','fastsam-x','yolov8n-layout'])
    ap.add_argument('--device', default='cpu')
    ap.add_argument('--runs', type=int, default=11)
    ap.add_argument('--threads', type=int, default=8)
    args = ap.parse_args()
    import torch
    torch.set_num_threads(args.threads)
    gpu = args.device != 'cpu'
    def sync():
        if gpu:
            torch.cuda.synchronize()
    OUT.mkdir(exist_ok=True)
    variant = args.model+'-'+args.device.replace(':','')
    load_start = time.perf_counter()
    if args.model == 'yolov8n-layout':
        import cv2
        from rapid_layout import RapidLayout
        from rapid_layout.model_handler.utils import ModelProcessor
        from rapid_layout.utils.typings import ModelType
        weight = Path(ModelProcessor.get_model_path(ModelType.YOLOV8N_LAYOUT_PAPER))
        model = RapidLayout(model_type='yolov8n_layout_paper',conf_thresh=.35)
        ort_opts = model.session.session.get_session_options()
        params = {'model_type':'yolov8n_layout_paper','conf':.35,'provider':model.session.session.get_providers(), 'precision':'FP32 ONNX','ort_intra_threads':ort_opts.intra_op_num_threads,'ort_inter_threads':ort_opts.inter_op_num_threads}
        def infer(image):
            r = model(cv2.cvtColor(np.asarray(image),cv2.COLOR_RGB2BGR))
            det = np.asarray(r.boxes).tolist()
            return [xywh(b) for b in det], {'xyxy':det,'scores':[float(v) for v in r.scores],'classes':list(r.class_names)}
    elif args.model.startswith('omni'):
        spec = importlib.util.spec_from_file_location('omni_yolov9', MODELS/'omni-yolov9.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        weight = MODELS/'omniparser-v3.pt'
        model = module.YOLOv9Detector(weight, device=args.device)
        params = {'imgsz':640, 'conf':.05, 'nms_iou':.1, 'max_det':300, 'precision':'FP16 autocast CUDA / FP32 CPU'}
        if args.model == 'omni-ocr':
            import easyocr
            reader = easyocr.Reader(['en'], gpu=gpu, model_storage_directory=str(MODELS/'easyocr'))
            tree = ast.parse((MODELS/'omni-utils.py').read_text(encoding='utf-8'))
            node = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'remove_overlap_new')
            scope = {'List':List}
            exec(compile(ast.Module(body=[node], type_ignores=[]), 'official_omni_overlap', 'exec'), scope)
            overlap = scope['remove_overlap_new']
            params.update({'ocr':'EasyOCR en', 'text_threshold':.9, 'paragraph':False, 'overlap_threshold':.7, 'caption':False})
        def infer(image):
            r = model.predict(image, conf=.05, imgsz=640, iou=.1)[0]
            det = r.boxes.xyxy.detach().cpu().tolist()
            scores = r.boxes.conf.detach().cpu().tolist()
            raw = {'xyxy':det,'scores':scores,'classes':['icon']*len(det)}
            if args.model == 'omni':
                return [xywh(b) for b in det], raw
            ocr = reader.readtext(np.asarray(image), paragraph=False, text_threshold=.9)
            w, h = image.size
            ob = [{'type':'text','bbox':[min(p[0] for p in poly)/w,min(p[1] for p in poly)/h,max(p[0] for p in poly)/w,max(p[1] for p in poly)/h], 'content':txt,'interactivity':False,'source':'box_ocr_content_ocr'} for poly,txt,score in ocr]
            # Match get_som_labeled_img's positive int_box_area filter before merge.
            ob = [v for v in ob if (int(v['bbox'][2]*w)-int(v['bbox'][0]*w))*(int(v['bbox'][3]*h)-int(v['bbox'][1]*h)) > 0]
            icons = [{'type':'icon','bbox':[b[0]/w,b[1]/h,b[2]/w,b[3]/h],'content':None,'interactivity':True} for b in det if (int(b[2])-int(b[0]))*(int(b[3])-int(b[1])) > 0]
            merged = overlap(icons,.7,ob)
            # The official no-OCR branch returns plain coordinate lists.
            bxs = [v['bbox'] if isinstance(v,dict) else v for v in merged]
            boxes = [xywh([b[0]*w,b[1]*h,b[2]*w,b[3]*h]) for b in bxs]
            raw.update(ocr=[{'quad':np.asarray(p).tolist(),'text':t,'score':float(s)} for p,t,s in ocr], merged=merged)
            return boxes,raw
    else:
        from ultralytics import FastSAM
        weight = MODELS/('FastSAM-s.pt' if args.model.endswith('s') else 'FastSAM-x.pt')
        model = FastSAM(str(weight))
        params = {'imgsz':1024,'conf':.4,'nms_iou':.9,'retina_masks':True,'precision':'FP32', 'mask_selection':'all rectangular bounding boxes, smallest containing query point first'}
        latest = [None]
        def infer(image):
            r = model.predict(image, device=args.device, imgsz=1024, conf=.4, iou=.9, retina_masks=True, verbose=False)[0]
            latest[0] = r
            det = r.boxes.xyxy.detach().cpu().tolist()
            raw = {'xyxy':det,'scores':r.boxes.conf.detach().cpu().tolist(),'classes':['segment']*len(det), 'mask_shape':list(r.masks.data.shape) if r.masks is not None else None}
            return [xywh(b) for b in det], raw
    sync()
    info = {'variant':variant,'load_ms':(time.perf_counter()-load_start)*1000,'device':args.device,'torch':torch.__version__,'cuda_runtime':torch.version.cuda,'gpu':torch.cuda.get_device_name(0) if torch.cuda.is_available() else None,'platform':platform.platform(),'python':platform.python_version(),'torch_threads':torch.get_num_threads(),'parameters':params,'weight':str(weight.relative_to(ROOT)),'weight_bytes':weight.stat().st_size,'weight_sha256':hashlib.sha256(weight.read_bytes()).hexdigest(),'timing':'PIL RGB image already loaded; synchronous model preprocess, inference, postprocess, CPU coordinate copy and OCR+merge if selected. Excludes disk read, load, capture, draw, point query. First sample per image separate; only first image is first inference in process.'}
    save(OUT/(variant+'-info.json'),info)
    manifest = json.loads((OLD/'manifest.json').read_text(encoding='utf-8'))
    output = []
    for case in manifest:
        image = Image.open(OLD/'inputs'/case['image']).convert('RGB')
        times = []
        for i in range(args.runs):
            sync()
            start = time.perf_counter()
            boxes, raw = infer(image)
            sync()
            times.append((time.perf_counter()-start)*1000)
        if args.model.startswith('fastsam') and case['image'] == manifest[0]['image'] and latest[0].masks is not None:
            # Evidence only, outside every measured interval: first eight masks.
            sample = np.asarray(image).astype(np.float32).copy()
            masks = latest[0].masks.data[:8].detach().cpu().numpy() > .5
            palette = np.array([[255,60,70],[40,170,255],[255,210,30],[40,240,120],[200,70,255],[255,140,30],[40,230,230],[220,100,150]])
            for mask,color in zip(masks,palette):
                sample[mask] = sample[mask]*.55+color*.45
            Image.fromarray(sample.astype(np.uint8)).save(OUT/(variant+'-first-eight-masks.jpg'),quality=90)
        save(OUT/(case['image']+'.'+variant+'-detections.json'),raw)
        output.append({'image':case['image'],'width':image.width,'height':image.height,'dpr':1,'preprocess_ms':times,'targets':queries(case,boxes),'all_boxes':boxes})
        save(OUT/(variant+'.json'),output)
        print(json.dumps({'variant':variant,'image':case['image'],'boxes':len(boxes),'first_ms':times[0],'p50_ms':float(np.median(times[1:])),'p95_ms':float(np.percentile(times[1:],95))}),flush=True)


if __name__ == '__main__':
    main()
