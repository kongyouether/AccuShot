"""Local same-capture comparison. Labels only score previously collected candidates.

python tests/contour/hybrid_report.py [--evidence DIR] [--annotations JSON]
Annotations: {"scene-id":{"point-id":{"name":"target", "target":[x,y,w,h]}}}.
Coordinates are snapshot-local Qt logical pixels; PNG pixels = logical * dpr.
"""
import argparse
import csv
import html
import json
import math
from pathlib import Path

MODES = ('Window', 'Element', 'Contour', 'Hybrid')


def iou(a, b):
    x = max(0, min(a[0]+a[2], b[0]+b[2])-max(a[0], b[0]))
    y = max(0, min(a[1]+a[3], b[1]+b[3])-max(a[1], b[1]))
    intersection = x*y
    union = a[2]*a[3]+b[2]*b[3]-intersection
    return intersection/union if union else 0


def percentile(values, p):
    values = sorted(v for v in values if v is not None and v >= 0)
    if not values:
        return None
    return round(values[max(0, math.ceil(len(values)*p)-1)], 4)


def table(rows):
    if not rows:
        return '<p>No labels yet. Candidate and latency evidence is available below.</p>'
    keys = list(rows[0])
    return '<div class="scroll"><table><tr>'+''.join('<th>'+html.escape(k)+'</th>' for k in keys)+'</tr>'+''.join('<tr>'+''.join('<td>'+html.escape(str(r[k]))+'</td>' for k in keys)+'</tr>' for r in rows)+'</table></div>'


def write_csv(path, rows):
    with path.open('w', newline='', encoding='utf-8-sig') as f:
        if rows:
            writer = csv.DictWriter(f, fieldnames=rows[0])
            writer.writeheader()
            writer.writerows(rows)


def target_png(evidence, scene, point, target, name):
    from PIL import Image, ImageDraw
    source = Image.open(evidence/scene['snapshot']).convert('RGB')
    dpr = scene['dpr']
    panels = []
    for mode in MODES:
        im = source.copy()
        draw = ImageDraw.Draw(im)
        def rectangle(rect, color, width):
            x, y, w, h = rect
            draw.rectangle((round(x*dpr), round(y*dpr), round((x+w)*dpr), round((y+h)*dpr)), outline=color, width=width)
        boxes = point['modes'][mode]['final_boxes']
        for rect in boxes:
            rectangle(rect, '#778899', 1)
        if boxes:
            rectangle(max(boxes, key=lambda r: iou(r, target)), '#009cff', 4)
            rectangle(boxes[0], '#ff4242', 4)
        rectangle(target, '#20dd65', 3)
        x, y = point['point']
        draw.ellipse((x*dpr-5, y*dpr-5, x*dpr+5, y*dpr+5), fill='#ffc400')
        # Natural target plus margin; candidate rectangles remain visible where they intersect.
        margin = 60*dpr
        x, y, w, h = target
        region = (max(0, int(x*dpr-margin)), max(0, int(y*dpr-margin)), min(im.width, int((x+w)*dpr+margin)), min(im.height, int((y+h)*dpr+margin)))
        crop = im.crop(region)
        crop.thumbnail((750, 500))
        panel = Image.new('RGB', (760, 540), '#f4f7f8')
        panel.paste(crop, (5, 35))
        ImageDraw.Draw(panel).text((10, 10), f'{mode} | red=default blue=best green=GT', fill='#17272d')
        panels.append(panel)
    grid = Image.new('RGB', (1520, 1080), 'white')
    for index, panel in enumerate(panels):
        grid.paste(panel, ((index % 2)*760, (index // 2)*540))
    dest = Path('targets')/(scene['scene']+'-'+str(point['id'])+'.png')
    (evidence/dest).parent.mkdir(exist_ok=True)
    grid.save(evidence/dest)
    return dest.as_posix()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path, default=Path(__file__).resolve().parents[2]/'docs/evidence/hybrid-snap-2026-10-18')
    parser.add_argument('--annotations', type=Path)
    args = parser.parse_args()
    evidence = args.evidence
    labels = json.loads(args.annotations.read_text(encoding='utf-8-sig')) if args.annotations else {}
    scenes = []
    for path in sorted(evidence.glob('*.json')):
        data = json.loads(path.read_text(encoding='utf-8-sig'))
        if isinstance(data, dict) and data.get('schema') == 1 and data.get('points') and not data.get('preview'):
            scenes.append(data)
    rows, timings, images = [], [], []
    for scene in scenes:
        for point in scene['points']:
            label = labels.get(scene['scene'], {}).get(str(point['id']), point)
            target = label.get('target')
            if target:
                point['target'] = target
                point['name'] = label.get('name', point['id'])
                images.append((scene['scene'], point['id'], label.get('name', point['id']), target_png(evidence, scene, point, target, label.get('name', ''))))
            for mode in MODES:
                result = point['modes'][mode]
                boxes = result['final_boxes']
                timings.append({'scene':scene['scene'], 'point':point['id'], 'mode':mode, 'query_ms':result['query_ms'], 'first_candidate_ms':result['first_candidate_ms'], 'ready_ms':result['ready_ms'], 'preprocess_ms':result['preprocess_ms'], 'candidates':len(boxes), 'foreground_unchanged':result['foreground_unchanged']})
                if not target:
                    continue
                overlaps = [iou(rect, target) for rect in boxes]
                initial = next((change['boxes'] for change in result['changes'] if change['boxes']), [])
                area_order = sorted(range(len(boxes)), key=lambda i:boxes[i][2]*boxes[i][3])
                start = area_order.index(0) if area_order else 0
                steps = [abs(area_order.index(i)-start) for i,v in enumerate(overlaps) if v >= .8]
                rows.append({'scene':scene['scene'], 'point':point['id'], 'target':label.get('name',point['id']), 'mode':mode, 'initial_default_iou':round(iou(initial[0],target),4) if initial else 0, 'settled_default_iou':round(overlaps[0],4) if overlaps else 0, 'top3_iou':round(max(overlaps[:3],default=0),4), 'any_iou':round(max(overlaps,default=0),4), 'wheel_min_steps':min(steps) if steps else -1, 'candidate_count':len(boxes), 'ready_ms':result['ready_ms']})
    summaries = []
    for mode in MODES:
        rr = [r for r in rows if r['mode'] == mode]
        tt = [t for t in timings if t['mode'] == mode]
        row = {'mode':mode, 'labelled_points':len(rr)}
        for metric in ('initial_default','settled_default','top3','any'):
            row[metric+'_hit_%'] = round(100*sum(r[metric+'_iou'] >= .8 for r in rr)/len(rr),1) if rr else None
        row['wheel_at_most_1_hit_%'] = round(100*sum(0 <= r['wheel_min_steps'] <= 1 for r in rr)/len(rr),1) if rr else None
        row.update({'query_samples':sum(len(t['query_ms']) for t in tt), 'query_p50_ms':percentile([q for t in tt for q in t['query_ms']], .5), 'query_p95_ms':percentile([q for t in tt for q in t['query_ms']], .95), 'first_candidate_p95_ms':percentile([t['first_candidate_ms'] for t in tt], .95), 'ready_p50_ms':percentile([t['ready_ms'] for t in tt], .5), 'ready_p95_ms':percentile([t['ready_ms'] for t in tt], .95), 'ready_timeouts':sum(t['ready_ms'] < 0 for t in tt), 'foreground_changed':sum(not t['foreground_unchanged'] for t in tt)})
        summaries.append(row)
    by_scene = []
    for scene in scenes:
        for mode in MODES:
            rr = [r for r in rows if r['scene'] == scene['scene'] and r['mode'] == mode]
            tt = [t for t in timings if t['scene'] == scene['scene'] and t['mode'] == mode]
            row = {'scene':scene['scene'], 'mode':mode, 'labelled_points':len(rr)}
            for metric in ('initial_default','settled_default','top3','any'):
                row[metric+'_hit_%'] = round(100*sum(r[metric+'_iou'] >= .8 for r in rr)/len(rr),1) if rr else None
            row['ready_p95_ms'] = percentile([t['ready_ms'] for t in tt], .95)
            row['query_samples'] = sum(len(t['query_ms']) for t in tt)
            row['query_p95_ms'] = percentile([q for t in tt for q in t['query_ms']], .95)
            by_scene.append(row)
    pre = [{'scene':s['scene'], 'capture_ms':round(s['capture_ms'],3), 'preprocess_cold_ms':round(s['preprocess_ms'][0],3), 'preprocess_warm_p50_ms':percentile(s['preprocess_ms'][1:], .5), 'preprocess_warm_p95_ms':percentile(s['preprocess_ms'][1:], .95), 'points':len(s['points']), 'foreground_unchanged':s['foreground_unchanged_at_end']} for s in scenes]
    (evidence/'summary.json').write_text(json.dumps({'scenes':pre,'summary':summaries,'by_scene':by_scene},ensure_ascii=False,indent=2),encoding='utf-8')
    write_csv(evidence/'points.csv', rows)
    write_csv(evidence/'summary.csv', summaries)
    write_csv(evidence/'scene-summary.csv', by_scene)
    overview = ''.join(f'<details><summary>{html.escape(s["scene"])}</summary><a href="{html.escape(s["scene"])}.json">Raw JSON</a><img src="{html.escape(s["snapshot"])}"></details>' for s in scenes)
    gallery = ''.join(f'<details open><summary>{html.escape(str(s))} / {html.escape(str(p))} / {html.escape(str(n))}</summary><img src="{html.escape(src)}"></details>' for s,p,n,src in images)
    data = json.dumps(scenes,ensure_ascii=False).replace('<','\\u003c')
    document = '''<!doctype html><meta charset="utf-8"><title>Hybrid snapping same-capture evidence</title><style>body{font:15px system-ui;margin:24px;background:#f4f7f8;color:#17272d}p{max-width:1200px;line-height:1.6}table{border-collapse:collapse;font-size:12px}td,th{border:1px solid #bbcbd0;padding:7px}img{max-width:100%}details{margin:12px 0;background:white;padding:10px}.scroll{overflow:auto}#stage{position:relative}#stage img{display:block;width:100%}#stage svg{position:absolute;inset:0;width:100%;height:100%}select{margin:8px}pre{white-space:pre-wrap}</style><h1>Hybrid snapping: same-capture local evidence</h1>
<p>All four modes link production SmartSnapper. UIA is queried during a static scene immediately after the saved snapshot. Window/content stability is a collection requirement; a stable foreground handle alone cannot prove unchanged content. Natural target rectangles are manual labels. No current UIA is used to fill old screenshots.</p>
<p>IoU ≥ 0.8 is a hit. Initial default is the first nonempty observation; settled default is the first candidate when production controlsReady is true and controlsPending is false. If a request times out, final observed candidates are still scored and the timeout is retained. Top3 is candidate/Tab order; wheel distance follows stable area order from that selected default and may go up or down. One step is one candidate, not a physical mouse detent guarantee.</p>
<p>Per-point first_candidate and ready start after snapshot preprocessing. Contour preprocessing is measured separately (one cold + ten warm runs). ready includes asynchronous UIA plus polling quantization. Timeout is recorded as -1 and excluded from ready percentiles, with timeout count retained. These are core/query measurements, not full overlay paint latency, input-to-photon latency or GUI FPS.</p><h2>Quality and latency</h2>'''+table(summaries)+'<h2>Per scene</h2>'+table(by_scene)+'<h2>Snapshot / preprocessing</h2>'+table(pre)+'''<h2>All candidates</h2><p>Choose a scene, query point and mode. Red is default, thin gray rectangles are all candidates, green is manual GT and yellow is the query point. Raw JSON contains source flags (Window=1, Element=2, Contour=4), owner, generation and request serial.</p><select id="scene"></select><select id="point"></select><select id="mode"></select><div id="stage"><img id="snapshot"><svg id="svg"></svg></div><pre id="detail"></pre><h2>Natural target comparisons</h2>'''+gallery+'<h2>Raw scenes</h2>'+overview+'<h2>Per-point metrics</h2>'+table(rows)+'''<script>const data='''+data+''';const $=x=>document.getElementById(x);for(let i=0;i<data.length;i++)$('scene').add(new Option(data[i].scene,i));for(const m of ['Window','Element','Contour','Hybrid'])$('mode').add(new Option(m,m));function points(){const s=data[$('scene').value];$('point').replaceChildren();if(!s)return;for(let i=0;i<s.points.length;i++)$('point').add(new Option(s.points[i].id,i));draw()}function draw(){const s=data[$('scene').value];if(!s)return;const p=s.points[$('point').value],m=p.modes[$('mode').value],svg=$('svg');$('snapshot').src=s.snapshot;svg.setAttribute('viewBox',`0 0 ${s.desktop[2]} ${s.desktop[3]}`);let h=m.final_boxes.map((r,i)=>`<rect x="${r[0]}" y="${r[1]}" width="${r[2]}" height="${r[3]}" fill="none" stroke="${i?'#778899':'#ff4242'}" stroke-width="${i?1:3}"/>`).join('');if(p.target){const r=p.target;h+=`<rect x="${r[0]}" y="${r[1]}" width="${r[2]}" height="${r[3]}" fill="none" stroke="#20dd65" stroke-width="3"/>`}h+=`<circle cx="${p.point[0]}" cy="${p.point[1]}" r="5" fill="#ffc400"/>`;svg.innerHTML=h;$('detail').textContent=JSON.stringify({point:p.point,target:p.target,mode:$('mode').value,...m},null,2)}$('scene').onchange=points;$('point').onchange=draw;$('mode').onchange=draw;points();</script>'''
    (evidence/'index.html').write_text(document,encoding='utf-8')
    print(json.dumps({'scenes':len(scenes),'labelled_points':len(rows)//4,'target_images':len(images),'summary':summaries},ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
