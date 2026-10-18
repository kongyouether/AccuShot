"""Score fresh production CV + historical native replay against deployed ranking v2.

No labels are edited and no current UIA is collected. Reuses the established IoU
and exact area-navigation metric, with independent candidate change accounting.
"""
import argparse
from collections import Counter
import html
import json
import os
from pathlib import Path

from hybrid_report import percentile, table, write_csv
from ranking_report import digest, load, metrics, overlay, SCENES, METRICS


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', type=Path, default=root/'docs/evidence/hybrid-snap-2026-10-18')
    parser.add_argument('--baseline', type=Path, default=root/'docs/evidence/hybrid-ranking-2026-10-18/v2')
    parser.add_argument('--evidence', type=Path, default=root/'docs/evidence/snap-refinement-2026-10-18/history-v1')
    args = parser.parse_args()
    source, baseline, output = args.source.resolve(), args.baseline.resolve(), args.evidence.resolve()
    manifest = load(output/'replay-manifest.json')
    rows, changes, timing, checks, gallery = [], [], [], [], []
    pairs = []
    cv_times, query_times, fusion_times = [], [], []
    for name in SCENES:
        captured, old, new = load(source/(name+'.json')), load(baseline/(name+'-replay.json')), load(output/(name+'-replay.json'))
        assert len(captured['points']) == len(old['points']) == len(new['points']) == 5
        assert digest(source/(name+'.json')) == new['source_json_sha256']
        assert digest(source/captured['snapshot']) == new['source_snapshot_sha256']
        assert digest(baseline/(name+'-replay.json')) == new['v2_replay_sha256']
        assert captured['snapshot'] == new['snapshot'] and captured['dpr'] == new['dpr']
        cv_times.extend(new['preprocess_ms'][1:])
        timing.append(dict(scene=name, operation='CV preprocessing', cold_first_ms=new['preprocess_ms'][0],
                           warm_samples=len(new['preprocess_ms'])-1,
                           warm_p50_ms=percentile(new['preprocess_ms'][1:], .5),
                           warm_p95_ms=percentile(new['preprocess_ms'][1:], .95)))
        for cap, prior, point in zip(captured['points'], old['points'], new['points']):
            assert all(cap[k] == prior[k] == point[k] for k in ('id', 'name', 'point', 'target'))
            assert prior['after_boxes'] == point['before_boxes']
            assert prior['after_details'] == point['before_details']
            recovered = [{**c, 'sources': c['sources'] & 3} for c in cap['modes']['Hybrid']['final_details'] if c['sources'] & 3]
            assert len(recovered) == len(point['historical_native_details'])
            for expected, actual in zip(recovered, point['historical_native_details']):
                assert all(actual[k] == expected[k] for k in ('rect', 'sources', 'depth', 'owner_window', 'generation', 'request_serial'))
                assert actual.get('control_type', 0) == 0
                assert actual['sources'] & 4 == 0
            assert point['after_boxes'] == [c['rect'] for c in point['after_details']]
            values = {}
            for version in ('before', 'after'):
                values[version] = metrics(point[version+'_boxes'], point['target'])
                rows.append(dict(scene=name, id=point['id'], name=point['name'], version=version,
                                 **values[version]))
            change = dict(scene=name, id=point['id'], name=point['name'])
            for metric in METRICS:
                a, b = values['before'][metric], values['after'][metric]
                change[metric] = 'improved' if b and not a else 'regressed' if a and not b else 'unchanged'
            before_boxes, after_boxes = list(map(tuple, point['before_boxes'])), list(map(tuple, point['after_boxes']))
            removed, added = Counter(before_boxes)-Counter(after_boxes), Counter(after_boxes)-Counter(before_boxes)
            change['removed_geometry_count'] = sum(removed.values())
            change['new_geometry_count'] = sum(added.values())
            changes.append(change)
            pairs.append(dict(scene=name, id=point['id'], name=point['name'], target=point['target'],
                              removed_boxes=list(removed.elements()), new_boxes=list(added.elements()),
                              same_geometry_count=sum((Counter(before_boxes)&Counter(after_boxes)).values())))
            query_times.extend(point['query_ms'])
            fusion_times.extend(point['fusion_rank_ms'])
            for label, samples in [('CV query', point['query_ms']), ('fusion and ranking', point['fusion_rank_ms'])]:
                timing.append(dict(scene=name+'/'+point['id'], operation=label, cold_first_ms=None,
                                   warm_samples=len(samples), warm_p50_ms=percentile(samples, .5),
                                   warm_p95_ms=percentile(samples, .95)))
            picture = overlay(source, output, new, point)
            gallery.append((name, point, change, picture))
        checks.append(dict(scene=name, original_png_sha256=new['source_snapshot_sha256'],
                           original_json_sha256=new['source_json_sha256'], baseline_sha256=new['v2_replay_sha256'],
                           unchanged_gt_and_points=True, historical_native_masked_and_unknown=True))
    summary = []
    for name in ('all',)+SCENES:
        for version in ('before', 'after'):
            group = [r for r in rows if r['version'] == version and (name == 'all' or r['scene'] == name)]
            summary.append(dict(scene=name, version=version, points=len(group),
                                **{metric+'_hits': sum(r[metric] for r in group) for metric in METRICS}))
    transitions = [dict(metric=m, improved=sum(r[m] == 'improved' for r in changes),
                        regressed=sum(r[m] == 'regressed' for r in changes),
                        unchanged=sum(r[m] == 'unchanged' for r in changes)) for m in METRICS]
    for label, samples in [('all CV preprocessing', cv_times), ('all CV query', query_times), ('all fusion and ranking', fusion_times)]:
        timing.append(dict(scene='all', operation=label, cold_first_ms=None, warm_samples=len(samples),
                           warm_p50_ms=percentile(samples, .5), warm_p95_ms=percentile(samples, .95)))
    write_csv(output/'points.csv', rows)
    write_csv(output/'changes.csv', changes)
    write_csv(output/'summary.csv', summary)
    write_csv(output/'timings.csv', timing)
    result = dict(scope='historical development set; current production CV plus old selected native geometry; no current UIA',
                  summary=summary, transitions=transitions, timings=timing, checks=checks, geometry_changes=pairs)
    (output/'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    before, after = summary[:2]
    description = '；'.join(f'{m}: {before[m+"_hits"]}/20 → {after[m+"_hits"]}/20' for m in ('top3', 'wheel_le2', 'any', 'top1'))
    cards = []
    for scene, p, change, picture in gallery:
        cards.append(f'<article><h3>{html.escape(scene+" / "+p["name"])}</h3><p>{html.escape(str(change))}</p>'
                     f'<a href="{picture}"><img loading="lazy" src="{picture}" alt="v2 and new candidate comparison"></a>'
                     f'<p>GT {p["target"]}；鼠标 {p["point"]}</p></article>')
    original_link = Path(os.path.relpath(source/'index.html', output)).as_posix()
    baseline_link = Path(os.path.relpath(baseline/'index.html', output)).as_posix()
    page = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>轮廓细化：固定四场景重测</title>
<style>body{{font:16px system-ui;max-width:1400px;margin:36px auto;padding:0 24px;color:#18332c;background:#f4f8f6}}h1,h2,h3{{line-height:1.3}}p{{line-height:1.75}}a{{color:#176d56}}table{{border-collapse:collapse;background:white;font:13px system-ui}}th,td{{padding:8px;border:1px solid #ccdcd5;text-align:left;white-space:nowrap}}.scroll{{overflow:auto}}article{{padding:16px;background:white;margin:22px 0;border:1px solid #ccdcd5;border-radius:10px}}img{{width:100%;height:auto}}code{{word-break:break-all}}</style>
<h1>轮廓细化：固定四场景重测</h1><p>{description}。命中阈值固定为 IoU≥0.8。</p>
<p>本报告只比较历史四类界面20点开发集。PNG、鼠标点、人工GT未改；重新运行当前生产CV，取旧Hybrid final_details中 Window/Element 候选并去掉CV位，经生产 fuseCandidates 融合和排序。基线为已部署 ranking v2 的固定输出。<a href="{original_link}">原始捕获</a> · <a href="{baseline_link}">ranking v2</a></p>
<p><strong>历史native没有controlType，全部保持未知，不能用本报告宣称原生语义提权改善。</strong>旧Hybrid已经过去重，保留的native矩形可能受到当时的几何选择影响，无法还原被淘汰的原始native集合。本报告不是新的同步UIA采集，也不是独立留出集。新现场证据另行汇总。</p>
<p>Top-3按推荐列表前三项；wheel_le2按生产覆盖层的稳定面积排序，从默认首项双向滚轮至命中框的最少步数≤2；any为完整列表至少一项命中。新框和消失框完整保留于 geometry_changes；不为改善结果修改标注或过滤失败点。</p>
<h2>配对结果</h2>{table(summary)}<h2>逐点改善与退化</h2>{table(transitions)}{table(changes)}
<h2>耗时</h2><p>CV每图21次：第一项单列，后20次为热分布；“第一项”不等于进程/机器冷启动。查询及融合排序每点重复{manifest['query_repeats']}次。计时不含PNG解码、当前UIA、GUI输入、绘制或动画。毫秒值按4位小数展示，原始JSON保留精度；极短查询易受计时器分辨率影响。</p>{table(timing)}
<h2>逐点原图对照</h2><p>左为部署v2，右为新CV+历史native。红=Top1、橙=Top2、紫=Top3、绿=人工GT、灰=全部候选、黄点=鼠标。</p>{''.join(cards)}
<h2>原件和完整数据</h2><p><a href="replay-manifest.json">运行清单/源码哈希</a> · <a href="summary.json">汇总与所有新/消失矩形</a> · <a href="points.csv">逐点指标</a> · <a href="timings.csv">计时CSV</a></p>
{''.join(f'<p><a href="{n}-replay.json">{n}完整候选与原始计时</a></p>' for n in SCENES)}{table(checks)}</html>'''
    (output/'index.html').write_text(page, encoding='utf-8')
    (output/'README.md').write_text(f'''# 历史固定开发集重测

{description}。

基线为 `hybrid-ranking-2026-10-18/v2`；图片/点/GT为 `hybrid-snap-2026-10-18` 原件，只读且按SHA256核对。工具源码 `tests/contour/refinement_replay.cpp` 和 `refinement_report.py`。从仓库根执行：

```powershell
$env:PATH = 'C:/Qt/6.11.1/msvc2022_64/bin;' + $env:PATH
./build-snap-refinement-eval/refinement_replay.exe --output docs/evidence/snap-refinement-2026-10-18/history-v1
python tests/contour/refinement_report.py --evidence docs/evidence/snap-refinement-2026-10-18/history-v1
```

首次需MSVC x64环境，以 `tests/contour/refinement` 为CMake source，独立目录 `build-snap-refinement-eval` 配置Release/Ninja/Qt6；不使用共享产品构建目录，不部署程序。replay拒绝覆盖旧JSON，迭代需新输出子目录。

历史native无controlType，保持未知；几何来自已融合去重的旧候选，存在选择偏差。生产CV与生产fusion/rank调用均无GT输入，不查询当前UIA。不是新采集、独立留出或全GUI延迟验收。极短查询数值受计时器分辨率影响。完整失败和候选增减保留在JSON、CSV和逐点图中。
''', encoding='utf-8')
    print(description)
    print(json.dumps(transitions, ensure_ascii=False))
    print(f'Wrote {output}/index.html')


if __name__ == '__main__':
    main()
