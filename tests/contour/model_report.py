"""Generate the independent model comparison; never overwrite the earlier report."""
import csv
import html
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
from report import iou
from model_benchmark import queries

ROOT = Path(__file__).resolve().parents[2]
OLD = ROOT/'docs/evidence/contour-2026-10-18'
OUT = ROOT/'docs/evidence/contour-models-2026-10-18'


def table(rows):
    if not rows:
        return ''
    return '<div class="scroll"><table><tr>'+''.join('<th>'+html.escape(str(k))+'</th>' for k in rows[0])+'</tr>'+''.join('<tr>'+''.join('<td>'+html.escape(str(v))+'</td>' for v in r.values())+'</tr>' for r in rows)+'</table></div>'


def main():
    manifest = json.loads((OLD/'manifest.json').read_text(encoding='utf-8'))
    split = {x['image']:x.get('split','development') for x in manifest}
    (OUT/'selections').mkdir(exist_ok=True)
    (OUT/'overlays').mkdir(exist_ok=True)
    sources = [(OLD/'current.json','current-C++-historical'),(OLD/'model.json','YOLOv8n-layout-CPU-historical'),(OLD/'model-doclayout.json','DocLayout-CPU-historical')]
    sources += [(p,p.stem) for p in sorted(OUT.glob('*.json')) if p.stem.startswith(('omni-','fastsam-','yolov8n-layout-')) and not p.stem.endswith(('-info','-scored')) and '.png.' not in p.stem]
    rows = []
    detail = []
    scores_by_variant = {}
    cv = {c['image']:c for c in json.loads((OLD/'current.json').read_text(encoding='utf-8'))}
    fusion_rows = []
    for path, variant in sources:
        data = json.loads(path.read_text(encoding='utf-8'))
        if not isinstance(data,list) or not data or 'targets' not in data[0]:
            continue
        total = []
        for case in data:
            if 'all_boxes' in case:
                # Rescore from stored model candidates using the exact Qt rounding.
                case['targets'] = queries(case,case['all_boxes'])
            im = Image.open(OLD/'inputs'/case['image']).convert('RGB')
            first, top3, any_, points = [], [], [], []
            target_details = []
            if case.get('all_boxes') is not None:
                allim = im.copy()
                d = ImageDraw.Draw(allim)
                for n,b in enumerate(case['all_boxes']):
                    x,y,w,h = b
                    color = ['#ff4956','#0af','#ed0','#50f090'][n%4]
                    d.rectangle((x,y,x+w,y+h),outline=color,width=2)
                    d.text((x+2,y+1),str(n),fill=color,stroke_width=1,stroke_fill='#102030')
                name = variant+'-'+Path(case['image']).stem+'.jpg'
                allim.save(OUT/'overlays'/name,quality=88)
                target_details.append('<p><a href="overlays/'+name+'">全部检测候选框</a></p>')
            for ti,target in enumerate(case['targets']):
                qrows = []
                for qi,q in enumerate(target['queries']):
                    vals = [iou(target['rect'],b) for b in q['boxes']]
                    a = vals[0] if vals else 0
                    b = max(vals[:3],default=0)
                    c = max(vals,default=0)
                    first.append(a)
                    top3.append(b)
                    any_.append(c)
                    points.append(q['point'])
                    total.append({'split':split[case['image']],'first':a,'top3':b,'any':c})
                    cvq = cv[case['image']]['targets'][ti]['queries'][qi]
                    if 'all_boxes' in case:
                        assert q['point'] == cvq['point'], (variant,case['image'],q['point'],cvq['point'])
                        union = sorted(cvq['boxes']+q['boxes'],key=lambda b:b[2]*b[3])
                        uv = [iou(target['rect'],b) for b in union]
                        cvv = [iou(target['rect'],b) for b in cvq['boxes']]
                        fusion_rows.append({'variant':variant,'image':case['image'],'target':target['name'],'point':str(q['point']),'cv_point':str(cvq['point']),'cv_first':cvv[0] if cvv else 0,'cv_any':max(cvv,default=0),'union_area_first':uv[0] if uv else 0,'union_area_top3':max(uv[:3],default=0),'union_any':max(uv,default=0)})
                    qrows.append({'point':str(q['point']),'候选数量':len(vals),'第一IoU':round(a,3),'前三最佳IoU':round(b,3),'全部最佳IoU':round(c,3),'成功':a>=.8})
                q = target['queries'][0]
                vals = [iou(target['rect'],b) for b in q['boxes']]
                selected = q['boxes'][0] if vals else None
                best = q['boxes'][int(np.argmax(vals))] if vals else None
                canvas = im.copy()
                d = ImageDraw.Draw(canvas)
                for b,color in [(target['rect'],'#22dd66'),(selected,'#ff4455'),(best,'#4488ff')]:
                    if b:
                        x,y,w,h = b
                        d.rectangle((x,y,x+w,y+h),outline=color,width=3)
                for qn in target['queries']:
                    px,py = qn['point']
                    d.ellipse((px-3,py-3,px+3,py+3),fill='#ffff00')
                name = variant+'-'+Path(case['image']).stem+'-'+target['name']+'.jpg'
                canvas.save(OUT/'selections'/name,quality=88)
                target_details.append('<details><summary>'+html.escape(target['name'])+'：中心第一 IoU '+str(round(vals[0],3) if vals else 0)+'</summary>'+table(qrows)+'<a href="selections/'+name+'"><img loading="lazy" src="selections/'+name+'"></a></details>')
            ms = case['preprocess_ms']
            row = {'variant':variant,'image':case['image'],'split':split[case['image']],'points':len(first),'first_success':round(float(np.mean(np.array(first)>=.8)),4),'top3_success':round(float(np.mean(np.array(top3)>=.8)),4),'any_success':round(float(np.mean(np.array(any_)>=.8)),4),'first_sample_ms':round(ms[0],2),'warm_p50_ms':round(float(np.median(ms[1:])),2),'warm_p95_ms':round(float(np.percentile(ms[1:],95)),2),'runs':len(ms)}
            rows.append(row)
            detail.append('<details><summary>'+html.escape(variant+' / '+case['image'])+'</summary>'+''.join(target_details)+'</details>')
        scores_by_variant[variant] = total
        if path.parent == OUT:
            (OUT/(variant+'-scored.json')).write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    summary=[]
    for variant,scores in scores_by_variant.items():
        subset=[r for r in rows if r['variant']==variant]
        for sp in ['all','development','holdout']:
            ss=[s for s in scores if sp=='all' or s['split']==sp]
            rr=[r for r in subset if sp=='all' or r['split']==sp]
            if ss:
                summary.append({'variant':variant,'split':sp,'points':len(ss),'first':f'{sum(s["first"]>=.8 for s in ss)}/{len(ss)}','first_%':round(100*np.mean([s['first']>=.8 for s in ss]),1),'top3_%':round(100*np.mean([s['top3']>=.8 for s in ss]),1),'any_%':round(100*np.mean([s['any']>=.8 for s in ss]),1),'image_warm_P50_range_ms':f'{min(r["warm_p50_ms"] for r in rr):.2f}–{max(r["warm_p50_ms"] for r in rr):.2f}','image_warm_P95_range_ms':f'{min(r["warm_p95_ms"] for r in rr):.2f}–{max(r["warm_p95_ms"] for r in rr):.2f}'})
    for filename,data in [('metrics.csv',rows),('summary.csv',summary)]:
        with (OUT/filename).open('w',newline='',encoding='utf-8-sig') as f:
            wr=csv.DictWriter(f,fieldnames=data[0]);wr.writeheader();wr.writerows(data)
    (OUT/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    fusion = []
    for variant in scores_by_variant:
        rr = [r for r in fusion_rows if r['variant']==variant]
        if not rr:
            continue
        fusion.append({'variant':variant,'points':len(rr), **{k+'_success_%':round(100*float(np.mean([r[k]>=.8 for r in rr])),1) for k in ['cv_first','cv_any','union_area_first','union_area_top3','union_any']}})
    (OUT/'fusion-summary.json').write_text(json.dumps(fusion,ensure_ascii=False,indent=2),encoding='utf-8')
    with (OUT/'fusion-points.csv').open('w',newline='',encoding='utf-8-sig') as f:
        wr=csv.DictWriter(f,fieldnames=fusion_rows[0]);wr.writeheader();wr.writerows(fusion_rows)
    infos = []
    for p in sorted(OUT.glob('*-info.json')):
        infos.append('<details><summary>'+html.escape(p.name)+'</summary><pre>'+html.escape(p.read_text(encoding='utf-8'))+'</pre></details>')
    intro='''<h1>本地模型与图像轮廓吸附：同一截图对照</h1>
<p>2026-10-18；七图、35个人工矩形、每矩形五点，共175点。五图为先前开发集，两图为先前同站点留出集。本轮模型使用固定参数，没有逐图调整；人工标注未经用户复核，这不是通用GUI基准。</p>
<p>成功标准沿用 IoU≥0.8。图像检测/分割仅接收像素，标注只在推理完成后决定查询点与评分。包含鼠标点的矩形按面积从小到大排列。第一候选是默认选框，前三表示有限Tab切换，任意候选表示可达到的上限。FastSAM使用分割模型输出矩形框供同口径评分；不是掩膜IoU，也不是官方多点提示的评分。</p>
<p>OmniParser v3 detector-only = 官方YOLOv9-E定位器。omni-ocr = 该定位器 + 官方支持的EasyOCR英语路径 + 官方remove_overlap_new融合（0.7）；不含Florence图标描述。因此两个结果均不能写成OmniParser完整语义解析管线准确率/耗时。OCR行框与论文段落、整卡片裁剪意图不一定一致。</p>
<p>FastSAM-s/x采用Ultralytics官方集成，1024、conf=0.4、NMS IoU=0.9、retina_masks=True、FP32；OmniParser按官方demo conf=0.05、NMS IoU=0.1、默认640，CUDA官方适配器为FP16 autocast。CPU/GPU是不同数值精度/后端，不强制输出完全一致。</p>
<p>每图11次，第一样本单列，后10次计算P50/P95。只有进程第一张图的第一样本是首次推理，后续第一样本是形状/内容切换的第一样本。计时包含已读入RGB图像的缩放、模型、后处理、同步与结果坐标回CPU；omni-ocr还包括OCR与融合。CUDA前后显式同步。模型加载另列，排除读盘、截图、绘制、鼠标查询。P95来自10次热样本，样本量有限；这不是端到端截屏延迟或视频帧率。</p>
<p>历史C++、YOLOv8n论文版面和DocLayout CPU结果直接引用前轮原件，标注historical；不是本轮同期计时。YOLOv8n具体为RapidLayout论文版面权重，并非COCO通用物体权重。</p>
<p>绿色人工目标；红色中心点第一候选；蓝色中心点最佳候选；黄色为五个点。每个目标下完整列出五点成功与失败，JSON保存全部候选。<a href="../contour-2026-10-18/manifest.json">标注原件</a> · <a href="metrics.csv">逐图CSV</a> · <a href="summary.csv">汇总CSV</a> · <a href="sources.json">权重来源</a> · <a href="dependencies.txt">依赖</a> · <a href="README.md">复现与限制</a></p>'''
    fusion_note='<h2>CV与模型候选并集：离线互补性探针</h2><p>CV保持前轮原排序作为本底；候选并集不去重，按矩形面积从小到大排序。union_any是可达到的覆盖上限，不是默认选框准确率。union_area_first/top3是这种朴素面积排序的实际命中，展示小框抢占。只用预存候选，不含生产接入、性能与交互验收；逐点见<a href="fusion-points.csv">CSV</a>。相同矩形重复会占用前三位置，因此它是字面拼接的结果。</p>'+table(fusion)
    doc='<!doctype html><meta charset="utf-8"><title>本地模型实测</title><style>body{font:15px system-ui;margin:28px;background:#f4f7f8;color:#17272d}p{max-width:1100px;line-height:1.7}table{border-collapse:collapse;font-size:12px}td,th{border:1px solid #bccbd0;padding:7px}th{background:#e3eef0}img{max-width:100%}details{padding:12px;background:white;margin:10px 0;border:1px solid #ccd9dc}summary{cursor:pointer}pre{white-space:pre-wrap}.scroll{overflow:auto}</style>'+intro+'<h2>汇总</h2>'+table(summary)+fusion_note+'<h2>逐图计时与质量</h2>'+table(rows)+'<h2>环境和加载</h2>'+''.join(infos)+'<h2>所有目标</h2>'+''.join(detail)
    (OUT/'index.html').write_text(doc,encoding='utf-8')
    print(json.dumps(summary,ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
