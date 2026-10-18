import json,csv,html
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw,ImageFont
E=Path(__file__).resolve().parents[2]/'docs/evidence/contour-2026-10-18'

def iou(a,b):
    x,y,w,h=a;u,v,s,t=b
    inter=max(0,min(x+w,u+s)-max(x,u))*max(0,min(y+h,v+t)-max(y,v))
    return inter/(w*h+s*t-inter) if w*h+s*t>inter else 0

def main():
    variants=[v for v in ['baseline','cv','model','model-doclayout','current'] if (E/(v+'.json')).exists()]
    splits={c['image']:c.get('split','development') for c in json.loads((E/'manifest.json').read_text())}
    rows=[];body=['<h1>图像轮廓吸附：选框与性能对照</h1><p>绿色：人工目标框；红色：默认第一候选；蓝色：该鼠标位置下最接近目标的候选；黄色：鼠标点。红蓝相同会叠在一起。成功标准为 IoU ≥ 0.8，每个目标取中心和四个四分位点。开发集参与了算法调整，留出集在算法冻结后采集，不能当作大规模通用准确率。</p><p>baseline=原版 C++；current=改进 C++；cv=Python 视觉原型；model=YOLOv8n 论文版面；model-doclayout=DocLayout-YOLO。耗时单位为毫秒，查询单位为微秒；模型/原型查询未测，留空。前处理耗时不含磁盘读取及屏幕捕获。点击展开查看每一个成功或失败案例。</p>']
    for variant in variants:
        data=json.loads((E/(variant+'.json')).read_text())
        for case in data:
            image=Image.open(E/'inputs'/case['image']).convert('RGB')
            top=[];oracle=[];top3=[];queries=[]
            for target in case['targets']:
                gt=[v/case.get('dpr',1) for v in target['rect']]
                qs=target['queries'];scores=[]
                for q in qs:
                    values=[iou(gt,b) for b in q['boxes']]
                    top.append(values[0] if values else 0);oracle.append(max(values,default=0));top3.append(max(values[:3],default=0));queries.append(q['query_us'])
                    scores.append(values)
                r=qs[0]['boxes'][0] if qs[0]['boxes'] else None
                best=qs[0]['boxes'][int(np.argmax(scores[0]))] if scores[0] else None
                canvas=image.copy();d=ImageDraw.Draw(canvas)
                for b,color in [(target['rect'],'#22dd66'),(r,'#ff4455'),(best,'#4488ff')]:
                    if b:
                        x,y,w,h=b;d.rectangle((x,y,x+w,y+h),outline=color,width=3)
                px,py=qs[0]['point'];d.ellipse((px-4,py-4,px+4,py+4),fill='yellow')
                name=f"{variant}-{Path(case['image']).stem}-{target['name']}.jpg"
                (E/'selections').mkdir(exist_ok=True);canvas.save(E/'selections'/name,quality=90)
                body.append(f'<details><summary>{variant} / {case["image"]} / {target["name"]}: centre first IoU={scores[0][0] if scores[0] else 0:.3f}, best={max(scores[0],default=0):.3f}</summary><a href="selections/{name}"><img loading="lazy" src="selections/{name}"></a></details>')
            prep=case['preprocess_ms'];row={'variant':variant,'image':case['image'],'split':splits[case['image']],'points':len(top),'first_success':round(float(np.mean(np.array(top)>=.8)),3),'top3_success':round(float(np.mean(np.array(top3)>=.8)),3),'any_success':round(float(np.mean(np.array(oracle)>=.8)),3),'first_mean_iou':round(float(np.mean(top)),3),'best_mean_iou':round(float(np.mean(oracle)),3),'first_ms':round(prep[0],2),'warm_p50_ms':round(float(np.median(prep[1:])),2),'warm_p95_ms':round(float(np.percentile(prep[1:],95)),2),'query_p95_us':round(float(np.percentile(queries,95)),2) if variant in ('baseline','current') else ''}
            rows.append(row)
    with (E/'metrics.csv').open('w',newline='',encoding='utf-8-sig') as f:
        writer=csv.DictWriter(f,fieldnames=rows[0]);writer.writeheader();writer.writerows(rows)
    table='<table><tr>'+''.join(f'<th>{k}</th>' for k in rows[0])+'</tr>'+''.join('<tr>'+''.join(f'<td>{v}</td>' for v in row.values())+'</tr>' for row in rows)+'</table>'
    (E/'index.html').write_text('<!doctype html><meta charset="utf-8"><title>轮廓吸附测试报告</title><style>body{font:15px system-ui;margin:32px;background:#f8fafb;color:#182830}table{border-collapse:collapse;font-size:12px}td,th{border:1px solid #ccd;padding:7px}img{max-width:100%;}details{margin:12px 0;padding:12px;background:white}summary{cursor:pointer}</style>'+body[0]+table+''.join(body[1:]),encoding='utf-8')
    examples=[('codex','composer'),('web','code_button'),('paper','paragraph'),('explorer','folder_icon')]
    sheet=Image.new('RGB',(1440,1760),'#eef2f5');draw=ImageDraw.Draw(sheet)
    font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',20)
    for row,(scene,target) in enumerate(examples):
        for col,variant in enumerate(['baseline','current']):
            frame=Image.open(E/'selections'/f'{variant}-{scene}-{target}.jpg')
            frame.thumbnail((710,400))
            sheet.paste(frame,(col*720,row*440+35))
            draw.text((col*720+8,row*440+5),f'{variant} | {scene} / {target}',font=font,fill='#17262f')
    sheet.save(E/'comparison.jpg',quality=92)
    for row in rows:print(row)

if __name__=='__main__':main()
