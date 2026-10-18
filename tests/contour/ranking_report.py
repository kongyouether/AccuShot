"""Score a production-ranker paired offline replay, preserving all old candidates and labels.

python tests/contour/ranking_report.py [--source DIR] [--evidence DIR]
Run ranking_replay first. This script never collects UIA, executes a detector or changes GT.
"""
import argparse
from collections import Counter
import hashlib
import html
import json
import os
from pathlib import Path

from hybrid_report import iou, percentile, table, write_csv

SCENES = ('codex-v2', 'explorer', 'web', 'paper')
METRICS = ('top1', 'top3', 'any', 'wheel_le2')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def metrics(boxes, target):
    overlaps = [iou(b, target) for b in boxes]
    # Exact area stable-sort used by SnippingOverlay::selectCandidateByArea.
    area_order = sorted(range(len(boxes)), key=lambda i: boxes[i][2]*boxes[i][3])
    start = area_order.index(0) if boxes else None
    hits = [(abs(rank-start), rank, i) for rank, i in enumerate(area_order) if overlaps[i] >= .8]
    nearest = min(hits) if hits else None
    return dict(candidate_count=len(boxes), top1_iou=overlaps[0] if overlaps else 0,
                top3_iou=max(overlaps[:3], default=0), any_iou=max(overlaps, default=0),
                top1=bool(overlaps and overlaps[0] >= .8), top3=max(overlaps[:3], default=0) >= .8,
                any=max(overlaps, default=0) >= .8,
                wheel_le2=bool(nearest is not None and nearest[0] <= 2),
                wheel_min_steps=nearest[0] if nearest else -1,
                default_area_rank=start+1 if start is not None else -1,
                nearest_hit_area_rank=nearest[1]+1 if nearest else -1,
                nearest_hit_candidate_rank=nearest[2]+1 if nearest else -1)


def overlay(source, output, scene, point):
    from PIL import Image, ImageDraw
    base = Image.open(source/scene['snapshot']).convert('RGB')
    dpr = scene['dpr']
    panels = []
    for version in ('before', 'after'):
        image = base.copy()
        draw = ImageDraw.Draw(image)

        def outline(r, color, width):
            x, y, w, h = r
            draw.rectangle((round(x*dpr), round(y*dpr), round((x+w)*dpr)-1,
                            round((y+h)*dpr)-1), outline=color, width=width)

        boxes = point[version+'_boxes']
        for r in boxes:
            outline(r, '#83909c', 1)
        for rank in reversed(range(min(3, len(boxes)))):
            outline(boxes[rank], ('#ff4242', '#ff9b16', '#dd40dc')[rank], 4 if rank == 0 else 3)
        outline(point['target'], '#20dd65', 3)
        x, y = point['point']
        draw.ellipse((x*dpr-5, y*dpr-5, x*dpr+5, y*dpr+5), fill='#ffd400')
        panel = Image.new('RGB', (base.width, base.height+38), '#f4f7f8')
        panel.paste(image, (0, 38))
        ImageDraw.Draw(panel).text((12, 12), f'{version}: red=Top1 orange=Top2 magenta=Top3 green=GT gray=all ({len(boxes)})', fill='#17272d')
        panels.append(panel)
    result = Image.new('RGB', (base.width*2, base.height+38), 'white')
    for i, panel in enumerate(panels):
        result.paste(panel, (i*base.width, 0))
    path = Path('overlays')/(scene['scene']+'-'+str(point['id'])+'.png')
    (output/path).parent.mkdir(exist_ok=True)
    result.save(output/path)
    return path.as_posix()


def summarize(rows, scene, version):
    selected = [r for r in rows if r['version'] == version and (scene == 'all' or r['scene'] == scene)]
    result = dict(scene=scene, version=version, points=len(selected))
    for m in METRICS:
        result[m+'_hits'] = sum(r[m] for r in selected)
        result[m+'_pct'] = round(100*result[m+'_hits']/len(selected), 1)
    return result


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=root/'docs/evidence/hybrid-snap-2026-10-18')
    parser.add_argument('--evidence', type=Path, default=root/'docs/evidence/hybrid-ranking-2026-10-18')
    parser.add_argument('--previous', type=Path, help='Preserved earlier iteration to link without rewriting it')
    args = parser.parse_args()
    source, output = args.source.resolve(), args.evidence.resolve()
    if source == output:
        raise ValueError('Source evidence must remain separate')
    manifest = load(output/'replay-manifest.json')
    rows, changes, timings, gallery, scenes = [], [], [], [], []
    warm, process_cold, first_point = [], [], []
    checks = []
    canonical = lambda candidate: json.dumps(candidate, sort_keys=True, separators=(',', ':'))
    for name in SCENES:
        original = load(source/(name+'.json'))
        replay = load(output/(name+'-replay.json'))
        assert original['scene'] == replay['scene'] == name
        assert len(original['points']) == len(replay['points']) == 5, name+' must retain all five points'
        assert digest(source/(name+'.json')) == replay['source_json_sha256'], 'Original JSON changed'
        assert digest(source/original['snapshot']) == replay['source_snapshot_sha256'], 'Original screenshot changed'
        assert original['dpr'] == replay['dpr'] and original['snapshot'] == replay['snapshot']
        for old, p in zip(original['points'], replay['points']):
            assert all(old[k] == p[k] for k in ('id', 'name', 'point', 'target')), 'GT or point changed'
            assert old['modes']['Hybrid']['final_details'] == p['before_details'], 'Old candidate details changed'
            assert old['modes']['Hybrid']['final_boxes'] == p['before_boxes'], 'Old candidate boxes changed'
            assert Counter(map(canonical, p['before_details'])) == Counter(map(canonical, p['after_details'])), 'Candidate multiset changed'
            order = p['after_original_indices']
            assert sorted(order) == list(range(len(p['before_details']))), 'Invalid original-index permutation'
            assert p['after_details'] == [p['before_details'][i] for i in order]
            assert p['after_boxes'] == [p['before_boxes'][i] for i in order]
            assert p['after_boxes'] == [c['rect'] for c in p['after_details']]
            versions = {}
            for version in ('before', 'after'):
                scored = metrics(p[version+'_boxes'], p['target'])
                versions[version] = scored
                rows.append(dict(scene=name, point=p['id'], target=p['name'], version=version, **scored))
            assert versions['before']['any_iou'] == versions['after']['any_iou'], 'Ranking changed available coverage'
            change = dict(scene=name, point=p['id'], target=p['name'], candidate_count=len(order),
                          before_top1_original_index=0, after_top1_original_index=order[0],
                          before_wheel_steps=versions['before']['wheel_min_steps'],
                          after_wheel_steps=versions['after']['wheel_min_steps'])
            for m in METRICS:
                a, b = versions['before'][m], versions['after'][m]
                change[m+'_change'] = 'improved' if b and not a else 'regressed' if a and not b else 'unchanged'
            changes.append(change)
            warm.extend(p['warm_ms'])
            first_point.append(p['first_call_ms'])
            if p['first_process_call']:
                process_cold.append(p['first_call_ms'])
            assert len(p['warm_ms']) == manifest['warm_repeats_per_point']
            timings.append(dict(scene=name, point=p['id'], candidate_count=len(order),
                first_call_ms=p['first_call_ms'], first_process_call=p['first_process_call'],
                warm_samples=len(p['warm_ms']), warm_p50_ms=percentile(p['warm_ms'], .5),
                warm_p95_ms=percentile(p['warm_ms'], .95), warm_max_ms=max(p['warm_ms'])))
            gallery.append((name, p['id'], p['name'], overlay(source, output, replay, p)))
        checks.append(dict(scene=name, points=5, original_json_sha256=replay['source_json_sha256'],
            original_png_sha256=replay['source_snapshot_sha256'], candidate_multisets_unchanged=True,
            gt_and_points_unchanged=True))
        replay['snapshot_url'] = Path(os.path.relpath(source/original['snapshot'], output)).as_posix()
        scenes.append(replay)
    assert len(process_cold) == 1, 'Only the first call in the replay process is process-cold'
    summaries = [summarize(rows, s, v) for s in ('all',)+SCENES for v in ('before', 'after')]
    deltas = []
    for m in METRICS:
        deltas.append(dict(metric=m, improved=sum(c[m+'_change'] == 'improved' for c in changes),
            regressed=sum(c[m+'_change'] == 'regressed' for c in changes),
            unchanged=sum(c[m+'_change'] == 'unchanged' for c in changes)))
    latency = dict(process_cold_samples=1, process_cold_ms=process_cold[0],
        first_call_per_point_samples=len(first_point), first_call_per_point_p95_ms=percentile(first_point,.95),
        warm_samples=len(warm), warm_p50_ms=percentile(warm,.5), warm_p95_ms=percentile(warm,.95),
        warm_max_ms=max(warm))
    summary = dict(kind='paired offline replay of previously captured production candidates',
        threshold_iou=.8, summary=summaries, metric_changes=deltas, latency=latency,
        invariants=checks, manifest=manifest, report_script_sha256=digest(Path(__file__)),
        replay_manifest_sha256=digest(output/'replay-manifest.json'))
    (output/'summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
    write_csv(output/'points.csv', rows)
    write_csv(output/'point-changes.csv', changes)
    write_csv(output/'summary.csv', summaries)
    write_csv(output/'ranking-timings.csv', timings)
    top = summaries[:2]
    previous = args.previous.resolve() if args.previous else None
    if previous is None and (output.parent/'replay-manifest.json').is_file():
        previous = output.parent
    iteration_md, iteration_html = [], ''
    if previous and previous != output:
        earlier = load(previous/'summary.json')['summary']
        earlier = next(r for r in earlier if r['scene'] == 'all' and r['version'] == 'after')
        earlier_url = Path(os.path.relpath(previous/'index.html', output)).as_posix()
        text = f'首轮保留结果：Top1 {earlier["top1_hits"]}/20、Top3 {earlier["top3_hits"]}/20、面积滚轮≤2 {earlier["wheel_le2_hits"]}/20。首轮Top3改善，但面积滚轮总命中未改善；首轮数据和失败目标继续保留。'
        iteration_md = ['', text, '', f'[首轮完整交互报告]({earlier_url})']
        iteration_html = '<h2>保留的首轮迭代</h2><p>'+html.escape(text)+f' <a href="{html.escape(earlier_url)}">首轮完整报告</a></p>'
    md = ['# 融合排序：固定候选配对离线重放', '',
        '四个实际界面既有同期截图，各5点，共20点。复用旧 Hybrid final_details，不重新采集 UIA 或运行图像检测；原目标、坐标、候选几何与元数据均逐项核对不变。', '',
        '| 指标（IoU≥0.8） | 重排前 | 重排后 |', '|---|---:|---:|']
    labels = dict(top1='Top1',top3='Top3（生产/Tab顺序）',any='任意候选',wheel_le2='面积滚轮双向最短≤2步')
    for m in METRICS:
        md.append(f'| {labels[m]} | {top[0][m+"_hits"]}/20 ({top[0][m+"_pct"]}%) | {top[1][m+"_hits"]}/20 ({top[1][m+"_pct"]}%) |')
    top3_delta = top[1]['top3_pct'] - top[0]['top3_pct']
    wheel_delta = top[1]['wheel_le2_pct'] - top[0]['wheel_le2_pct']
    md += ['', f'Top3变化 {top3_delta:+.1f} 个百分点；面积滚轮≤2步变化 {wheel_delta:+.1f} 个百分点。逐点改善和退化均在下表保留，不能将整体命中变化说成每个目标都改善。', '',
        '| 发生变化的点 | Top1变化 | Top3变化 | 面积滚轮最短步数（前→后） |', '|---|---|---|---|']
    for c in changes:
        if any(c[m+'_change'] != 'unchanged' for m in METRICS):
            md.append(f'| {c["scene"]}/{c["point"]} | {c["top1_change"]} | {c["top3_change"]} | {c["before_wheel_steps"]}→{c["after_wheel_steps"]} |')
    md += ['', 'Top3与面积滚轮≤2步分别计算，不等价。滚轮从每版默认框（列表首项）在面积稳定升序中的位置开始，取向大/向小候选最近命中距离，端点停止、不循环；一个步长指一个候选。异步覆盖层保留既选框的路径不在本重放中模拟。', '',
        f'计时仅调用生产 rankCandidates：进程首调 {process_cold[0]:.6f} ms（1样本）；热调用 {len(warm)} 次，P50 {latency["warm_p50_ms"]} ms / P95 {latency["warm_p95_ms"]} ms。其余每点首次调用已在同一进程中，不能称作独立进程冷启动。输入复制在计时外；截图、CV、UIA、绘制及全GUI延迟均不包含，不能与旧查询总耗时直接相减。', '',
        '这20点已参与本轮排序开发，属于开发集配对结果，不是独立留出准确率。纯排序不能提高任意候选覆盖；全部失败目标与小框保留。', '',
        '逐点改进/退化见 point-changes.csv，完整前后候选和索引见 *-replay.json，源码/二进制/原始输入SHA256见 replay-manifest.json。index.html提供原图交互叠框和20张完整原图前后对照；原始证据目录不改写。', '',
        '复现：先按本目录README构建运行 ranking_replay，再运行 `python tests/contour/ranking_report.py`。']
    md += iteration_md
    (output/'REPORT.md').write_text('\n'.join(md)+'\n',encoding='utf-8')
    gallery_html = ''.join(f'<details><summary>{html.escape(s)} / {html.escape(str(p))} / {html.escape(n)}</summary><a href="{src}">完整原图对照PNG</a><img loading="lazy" src="{src}"></details>' for s,p,n,src in gallery)
    data = json.dumps(scenes,ensure_ascii=False).replace('<','\\u003c')
    document = '''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>融合排序配对离线重放</title>
<style>body{font:15px system-ui;margin:24px;background:#f4f7f8;color:#17272d}p{line-height:1.65;max-width:1150px}table{border-collapse:collapse;font-size:12px}td,th{border:1px solid #bbcbd0;padding:7px}.scroll{overflow:auto}details{background:white;padding:12px;margin:12px 0}img{max-width:100%}select{margin:8px}#stage{position:relative}#stage img{display:block;width:100%}#stage svg{position:absolute;inset:0;width:100%;height:100%}pre{white-space:pre-wrap}</style>
<h1>融合排序：固定候选配对离线重放</h1><p>四个实际界面既有同期截图，各5点，共20点。直接调用生产 SmartSnapper::rankCandidates 重排旧 Hybrid final_details。没有重新采集 UIA、截图或CV；GT、查询点、全部小框与候选元数据保持不变。这20点参与排序开发，结果属于开发集配对重放，不是独立留出准确率。</p>
<p>IoU≥0.8为命中。Top3按生产/Tab列表顺序；滚轮≤2从每版默认框（首项）开始，按面积稳定升序取双向最短命中步数，端点停止、不循环。这两个指标分别计算且不等价。未模拟异步覆盖层保留既选框的状态。</p>'''+iteration_html+'''<h2>总计与逐场景</h2>'''+table(summaries)+'''<h2>逐点改进与退化计数</h2>'''+table(deltas)+'''<h2>纯排序计时</h2><p>只有进程首次调用是本进程冷调用（单样本）；每点first_call其余值不能称为独立冷启动。热调用每次使用原始候选，输入复制位于计时外。计时不含截图、CV、UIA、输入、覆盖层创建或绘制，不代表全GUI耗时；也不能直接与历史query耗时相减。</p>'''+table([latency])+table(timings)+'''<h2>原图交互叠框</h2><p>红=Top1，橙=Top2，紫=Top3，灰=全部候选，绿=GT，黄=查询点。切换版本查看全部小框，JSON保留全部详情。</p><select id="scene"></select><select id="point"></select><select id="version"><option value="before">重排前</option><option value="after">重排后</option></select><div id="stage"><img id="snapshot"><svg id="svg"></svg></div><pre id="detail"></pre><h2>20张原图配对叠框</h2>'''+gallery_html+'''<h2>逐点结果</h2>'''+table(rows)+'''<h2>逐点变化</h2>'''+table(changes)+'''<p><a href="replay-manifest.json">源码/二进制/输入清单</a> · <a href="summary.json">汇总JSON</a> · <a href="points.csv">逐点评分CSV</a> · <a href="point-changes.csv">变化CSV</a> · <a href="REPORT.md">文字报告</a></p>
<script>const data='''+data+''';const $=x=>document.getElementById(x);for(let i=0;i<data.length;i++)$('scene').add(new Option(data[i].scene,i));function points(){const s=data[$('scene').value];$('point').replaceChildren();for(let i=0;i<s.points.length;i++)$('point').add(new Option(s.points[i].id,i));draw()}function draw(){const s=data[$('scene').value],p=s.points[$('point').value],v=$('version').value,svg=$('svg');$('snapshot').src=s.snapshot_url;svg.setAttribute('viewBox',`0 0 ${s.image_pixels[0]/s.dpr} ${s.image_pixels[1]/s.dpr}`);const b=p[v+'_boxes'];let h=b.map(r=>`<rect x="${r[0]}" y="${r[1]}" width="${r[2]}" height="${r[3]}" fill="none" stroke="#83909c" stroke-width="1"/>`).join('');for(let i=Math.min(2,b.length-1);i>=0;i--){const r=b[i];h+=`<rect x="${r[0]}" y="${r[1]}" width="${r[2]}" height="${r[3]}" fill="none" stroke="${['#ff4242','#ff9b16','#dd40dc'][i]}" stroke-width="3"/>`}const r=p.target;h+=`<rect x="${r[0]}" y="${r[1]}" width="${r[2]}" height="${r[3]}" fill="none" stroke="#20dd65" stroke-width="3"/><circle cx="${p.point[0]}" cy="${p.point[1]}" r="5" fill="#ffd400"/>`;svg.innerHTML=h;$('detail').textContent=JSON.stringify({point:p.point,target:p.target,version:v,details:p[v+'_details'],original_indices:v==='after'?p.after_original_indices:b.map((_,i)=>i)},null,2)}$('scene').onchange=points;$('point').onchange=draw;$('version').onchange=draw;points();</script></html>'''
    (output/'index.html').write_text(document,encoding='utf-8')
    print(json.dumps(dict(summary=summaries[:2],metric_changes=deltas,latency=latency,overlays=len(gallery)),ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
