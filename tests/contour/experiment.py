"""Local experiments only. Inputs and annotations are never sent to a service."""
import json, time, sys
from pathlib import Path
import cv2
import numpy as np

ROOT=Path(__file__).resolve().parents[2]
E=ROOT/'docs/evidence/contour-2026-10-18'

def candidates(im):
    h0,w0=im.shape[:2]; scale=2
    gray=cv2.cvtColor(cv2.resize(im,(w0//scale,h0//scale),interpolation=cv2.INTER_AREA),cv2.COLOR_BGR2GRAY)
    h,w=gray.shape
    gx=cv2.Sobel(gray,cv2.CV_16S,1,0);gy=cv2.Sobel(gray,cv2.CV_16S,0,1)
    edge=((np.abs(gx)+np.abs(gy))>36).astype('uint8')
    boxes=[]
    def add(mask,kind,rx=0,ry=0):
        n,labels,stats,_=cv2.connectedComponentsWithStats(mask,4)
        for x,y,bw,bh,area in stats[1:]:
            if bw<12 or bh<7:continue
            if kind==0 and (x==0 or y==0 or x+bw>=w or y+bh>=h or area/(bw*bh)<.65):continue
            if kind and (bw> w*.9 or bh>h*.9):continue
            r=[int((x+rx)*scale),int((y+ry)*scale),int((bw-2*rx)*scale),int((bh-2*ry)*scale)]
            if r[2]>=24 and r[3]>=12:boxes.append((kind,r))
    add(((np.abs(gx)+np.abs(gy))<=12).astype('uint8'),0)
    add(1-edge,0)
    add(cv2.dilate(edge,np.ones((3,5),np.uint8)),1,2,1)
    add(cv2.dilate(edge,np.ones((9,25),np.uint8)),2,12,4)
    boxes.sort(key=lambda v:(0 if v[0]==0 and v[1][2]*v[1][3]<w0*h0*.4 else 1,v[1][2]*v[1][3]))
    return [b for _,b in boxes]+[[0,0,w0,h0]]

def main():
    manifest=json.loads((E/'manifest.json').read_text())
    mode=sys.argv[1] if len(sys.argv)>1 else 'cv'
    model_type=sys.argv[2] if len(sys.argv)>2 else 'yolov8n_layout_paper'
    variant='model-doclayout' if model_type=='doclayout_docstructbench' else mode
    if mode=='model':
        from rapid_layout import RapidLayout
        start=time.perf_counter()
        engine=RapidLayout(model_type=model_type,conf_thresh=.35)
        (E/(variant+'-info.json')).write_text(json.dumps({'model':model_type,'load_ms':(time.perf_counter()-start)*1000,'provider':engine.session.session.get_providers()}))
    output=[]
    for case in manifest:
        im=cv2.imread(str(E/'inputs'/case['image']));times=[]
        for _ in range(11):
            start=time.perf_counter()
            if mode=='model':
                res=engine(im)
                boxes=[[round(float(x)),round(float(y)),round(float(x2-x)),round(float(y2-y))] for x,y,x2,y2 in res.boxes]
            else: boxes=candidates(im)
            times.append((time.perf_counter()-start)*1000)
        if mode=='model':
            (E/(case['image']+'.'+variant+'-detections.json')).write_text(json.dumps({'boxes':boxes,'classes':list(res.class_names),'scores':[float(v) for v in res.scores]}))
        targets=[]
        for target in case['targets']:
            x,y,w,h=target['rect'];queries=[]
            for fx,fy in [(.5,.5),(.25,.25),(.75,.25),(.25,.75),(.75,.75)]:
                px,py=round(x+w*fx),round(y+h*fy)
                matched=[b for b in boxes if b[0]<=px<b[0]+b[2] and b[1]<=py<b[1]+b[3]]
                if mode=='model':matched.sort(key=lambda b:b[2]*b[3])
                queries.append({'point':[px,py],'boxes':matched,'query_us':0})
            targets.append(dict(target,queries=queries))
        output.append({'image':case['image'],'width':im.shape[1],'height':im.shape[0],'dpr':1,'preprocess_ms':times,'targets':targets})
    (E/(variant+'.json')).write_text(json.dumps(output,indent=2))

if __name__=='__main__':main()
