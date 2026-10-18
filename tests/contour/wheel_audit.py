"""Score area-ordered navigation against saved production candidates; no inference."""
import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'docs/evidence/contour-2026-10-18/current.json'
OUT = ROOT / 'docs/evidence/contour-wheel-2026-10-18'


def iou(a, b):
    x, y, w, h = a
    u, v, s, t = b
    intersection = max(0, min(x+w, u+s)-max(x, u))*max(0, min(y+h, v+t)-max(y, v))
    return intersection / (w*h+s*t-intersection)


def main():
    rows = []
    for case in json.loads(SOURCE.read_text(encoding='utf-8')):
        for target in case['targets']:
            gt = [v/case['dpr'] for v in target['rect']]
            for query in target['queries']:
                boxes = query['boxes']
                order = sorted(range(len(boxes)), key=lambda i: boxes[i][2]*boxes[i][3])
                start = order.index(0) if boxes else 0
                hits = [rank for rank, i in enumerate(order) if iou(gt, boxes[i]) >= .8]
                nearest = min(hits, key=lambda rank: abs(rank-start)) if hits else None
                rows.append(dict(image=case['image'], target=target['name'],
                    point_x=query['point'][0], point_y=query['point'][1],
                    candidates=len(boxes), first_success=bool(boxes and iou(gt, boxes[0]) >= .8),
                    smallest_success=bool(boxes and iou(gt, boxes[order[0]]) >= .8),
                    reachable=bool(hits),
                    minimum_wheel_steps=abs(nearest-start) if hits else '',
                    direction=('none' if nearest == start else 'down' if nearest > start else 'up') if hits else '',
                    correct_area_rank=(nearest+1) if hits else ''))
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT/'queries.csv').open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0])
        writer.writeheader()
        writer.writerows(rows)
    groups = {'all': rows}
    groups.update({name: [r for r in rows if r['image'] == name] for name in dict.fromkeys(r['image'] for r in rows)})
    summary = {}
    for name, subset in groups.items():
        summary[name] = dict(points=len(subset),
            first_success=sum(r['first_success'] for r in subset),
            smallest_success=sum(r['smallest_success'] for r in subset),
            any_candidate_success=sum(r['reachable'] for r in subset),
            within_one_step=sum(r['reachable'] and r['minimum_wheel_steps'] <= 1 for r in subset),
            within_two_steps=sum(r['reachable'] and r['minimum_wheel_steps'] <= 2 for r in subset),
            maximum_required_steps=max((r['minimum_wheel_steps'] for r in subset if r['reachable']), default=0))
    (OUT/'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
