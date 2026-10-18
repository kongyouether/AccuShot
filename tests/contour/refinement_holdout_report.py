"""Paired same-capture holdout scoring of frozen v2 core and frozen new core.

Both C++ runs must use --native-mode live and the exact same capture directory.
The report does not send labels or images to the algorithm implementation agent.
"""
import argparse
import html
import json
import os
from collections import Counter
from pathlib import Path

from hybrid_report import percentile, table, write_csv
from ranking_report import digest, load, metrics, overlay, METRICS


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--evidence', required=True, type=Path)
    parser.add_argument('--cohort', choices=('holdout', 'regression', 'recheck'), default='holdout')
    parser.add_argument('--context-note', default='')
    args = parser.parse_args()
    source, baseline, output = args.source.resolve(), args.baseline.resolve(), args.evidence.resolve()
    title = dict(holdout='新现场独立留出集配对验证', regression='已曝光现场集：修复后回归',
                 recheck='先前留出集：最终版本复核')[args.cohort]
    exposure = dict(holdout='本组目标和输出未反馈给CV/语义代理调参；主线负责标注，属于同工作流的小样本留出，不是外部盲测。',
                    regression='主线已查看stage1失败并据此保守修正语义排序，本组现在是修复回归集，不能再称独立留出集。stage1原失败保留。',
                    recheck='本组先前留出结果的汇总已知；本次通用规则收敛未针对本组目标修改，但这是最终版本复核，不能作为新的首次独立留出。首次留出原件另行保留。')[args.cohort]
    old_manifest, manifest = load(baseline/'replay-manifest.json'), load(output/'replay-manifest.json')
    assert old_manifest['native_mode'] == manifest['native_mode'] == 'live'
    names = [v['scene'] for v in manifest['inputs']]
    assert names == [v['scene'] for v in old_manifest['inputs']]
    assert old_manifest['smart_snapper_cpp_sha256'] != manifest['smart_snapper_cpp_sha256']
    rows, transitions, geometry, timings, pictures, checks, types = [], [], [], [], [], [], Counter()
    for name in names:
        cap = load(source/(name+'.json'))
        old, new = load(baseline/(name+'-replay.json')), load(output/(name+'-replay.json'))
        for key, path in [('source_json_sha256', source/(name+'.json')), ('source_snapshot_sha256', source/cap['snapshot'])]:
            assert old[key] == new[key] == digest(path)
        assert len(cap['points']) == len(old['points']) == len(new['points'])
        for version, replay in [('before', old), ('after', new)]:
            samples = replay['preprocess_ms']
            timings.append(dict(scene=name, version=version, operation='CV preprocess', first_ms=samples[0],
                                warm_samples=len(samples)-1, p50_ms=percentile(samples[1:], .5), p95_ms=percentile(samples[1:], .95)))
            for operation, key in [('CV query', 'query_ms'), ('fusion and rank', 'fusion_rank_ms')]:
                samples = [t for p in replay['points'] for t in p[key]]
                timings.append(dict(scene=name, version=version, operation=operation, first_ms=None,
                                    warm_samples=len(samples), p50_ms=percentile(samples, .5), p95_ms=percentile(samples, .95)))
        for cp, op, np in zip(cap['points'], old['points'], new['points']):
            assert all(cp[k] == op[k] == np[k] for k in ('id', 'name', 'target', 'point'))
            assert op['historical_native_details'] == np['historical_native_details']
            for native in np['historical_native_details']:
                assert native['sources'] & 4 == 0
                types[native['control_type']] += 1
            recovered = [c for c in cp['modes']['Element']['final_details'] if c['sources'] & 3]
            assert len(np['historical_native_details']) >= len(recovered)
            for expected, actual in zip(recovered, np['historical_native_details']):
                assert expected['rect'] == actual['rect'] and expected['sources'] & 3 == actual['sources']
                assert expected.get('control_type', 0) == actual['control_type']
            point = {**np, 'before_boxes': op['after_boxes'], 'before_details': op['after_details']}
            scored = {}
            category = cp.get('category', name)
            for version in ('before', 'after'):
                scored[version] = metrics(point[version+'_boxes'], point['target'])
                rows.append(dict(scene=name, category=category, id=point['id'], name=point['name'],
                                 target_key=json.dumps(point['target']), version=version, **scored[version]))
            change = dict(scene=name, category=category, id=point['id'], name=point['name'])
            for metric in METRICS:
                a, b = scored['before'][metric], scored['after'][metric]
                change[metric] = 'improved' if b and not a else 'regressed' if a and not b else 'unchanged'
            a, b = Counter(map(tuple, point['before_boxes'])), Counter(map(tuple, point['after_boxes']))
            change.update(new_geometry_count=sum((b-a).values()), removed_geometry_count=sum((a-b).values()))
            geometry.append(dict(scene=name, id=point['id'], new_boxes=list((b-a).elements()), removed_boxes=list((a-b).elements())))
            transitions.append(change)
            path = overlay(source, output, new, point)
            pictures.append(f'<article><h3>{html.escape(name+" / "+point["name"])}</h3><p>{html.escape(str(change))}</p><a href="{path}"><img src="{path}" loading="lazy"></a></article>')
        checks.append(dict(scene=name, points=len(cap['points']), png_sha256=new['source_snapshot_sha256'],
                           capture_sha256=new['source_json_sha256'], identical_native=True,
                           foreground_unchanged=cap.get('foreground_unchanged_at_end')))
    summary = []
    categories = sorted({row['category'] for row in rows})
    for name in ['all']+categories:
        for version in ('before', 'after'):
            group = [r for r in rows if r['version'] == version and (name == 'all' or r['category'] == name)]
            summary.append(dict(category=name, version=version, points=len(group),
                                targets=len({(r['scene'], r['name'], r['target_key']) for r in group}),
                                **{m+'_hits': sum(r[m] for r in group) for m in METRICS}))
    target_summary = []
    for key in sorted({(r['scene'], r['category'], r['name'], r['target_key']) for r in rows}):
        for version in ('before', 'after'):
            group = [r for r in rows if (r['scene'], r['category'], r['name'], r['target_key']) == key and r['version'] == version]
            target_summary.append(dict(scene=key[0], category=key[1], name=key[2], target=key[3], version=version,
                                       cursor_points=len(group), **{m+'_hit_points': sum(r[m] for r in group) for m in METRICS}))
    target_aggregate = []
    for category in ['all']+categories:
        for version in ('before', 'after'):
            group = [r for r in target_summary if r['version'] == version and (category == 'all' or r['category'] == category)]
            target_aggregate.append(dict(category=category, version=version, targets=len(group),
                                         **{m+'_all_cursor_points': sum(r[m+'_hit_points'] == r['cursor_points'] for r in group) for m in METRICS},
                                         **{m+'_at_least_one_cursor_point': sum(r[m+'_hit_points'] > 0 for r in group) for m in METRICS}))
    transition_summary = [dict(metric=m, improved=sum(c[m] == 'improved' for c in transitions),
                               regressed=sum(c[m] == 'regressed' for c in transitions), unchanged=sum(c[m] == 'unchanged' for c in transitions)) for m in METRICS]
    n = summary[0]['points']
    description = '；'.join(f'{m}: {summary[0][m+"_hits"]}/{n} → {summary[1][m+"_hits"]}/{n}' for m in ('top3', 'wheel_le2', 'any', 'top1'))
    result = dict(scope='same-capture '+args.cohort+'; identical typed Element native plus owner Window; frozen old/new production CV/fusion/rank',
                  cohort=args.cohort, exposure=exposure, context_note=args.context_note,
                  summary=summary, transitions=transition_summary, changes=transitions, timings=timings, geometry_changes=geometry,
                  checks=checks, native_control_type_occurrences=dict(types), target_summary=target_summary,
                  target_aggregate=target_aggregate,
                  before_core_sha256=old_manifest['smart_snapper_cpp_sha256'], after_core_sha256=manifest['smart_snapper_cpp_sha256'])
    (output/'summary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    for file, data in [('summary.csv', summary), ('points.csv', rows), ('changes.csv', transitions), ('timings.csv', timings),
                       ('target-summary.csv', target_summary), ('target-aggregate.csv', target_aggregate)]:
        write_csv(output/file, data)
    baseline_link = Path(os.path.relpath(baseline/'replay-manifest.json', output)).as_posix()
    raw_link = Path(os.path.relpath(source, output)).as_posix()
    data_links = ''.join(f'<p>{html.escape(name)}：<a href="{name}-replay.json">新版全部候选/计时</a> · '
                         f'<a href="{Path(os.path.relpath(baseline/(name+"-replay.json"), output)).as_posix()}">v2全部候选/计时</a></p>' for name in names)
    page = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>{title}</title>
<style>body{{font:16px system-ui;max-width:1400px;margin:36px auto;padding:0 24px;color:#18332c;background:#f4f8f6}}p{{line-height:1.75}}a{{color:#176d56}}table{{border-collapse:collapse;background:white;font-size:13px}}th,td{{padding:8px;border:1px solid #ccdcd5;white-space:nowrap}}.scroll{{overflow:auto}}article{{background:white;margin:24px 0;padding:16px}}img{{width:100%;height:auto}}</style>
<h1>{title}</h1><p>{description}。IoU≥0.8；全部失败保留。</p>
<p><strong>{exposure}</strong> {html.escape(args.context_note)}</p>
<p>当前真实界面截图与同期Window/Element候选独立采集。算法冻结后，以相同PNG、相同typed native、相同点和人工GT，分别运行冻结v2生产CV/fuse/rank与新生产CV/fuse/rank。native取Element final_details并去CV位，仅缺owner Window时从Window模式补入。两次运行均不查询当前UIA；旧排序不使用controlType。</p>
<p>这是整套新算法相对整套v2的配对比较，不能把所有差值单独归因为语义或CV。目标选择与人工标注仍是小样本；不与历史开发集混合，不代表普遍准确率。Top3为推荐前三，wheel_le2为按面积稳定排序从默认项双向≤2步可达，any为全部候选至少一项命中。</p>
<p><strong>同一目标的三个鼠标点高度相关，不能视为三个独立样本。</strong>表中同时报告点级命中和目标级结果：all_cursor_points要求同一目标全部采样点命中；at_least_one_cursor_point仅要求至少一个点命中。分类来自采集时预先标注的category。</p>
<h2>按界面类别的点级结果</h2>{table(summary)}{table(transition_summary)}
<h2>目标级结果</h2>{table(target_aggregate)}{table(target_summary)}<h2>逐点变化</h2>{table(transitions)}
<h2>时间</h2><p>每图21次CV，第1次单列，其余20次热分布；查询和融合每点{manifest['query_repeats']}次。含生产CV/融合排序，排除PNG解码、新UIA采集、GUI输入、绘制和动画。原始JSON保留未取整精度。</p>{table(timings)}
<h2>逐点图</h2><p>左v2，右新版；红Top1、橙Top2、紫Top3、绿GT、灰全部、黄鼠标。</p>{''.join(pictures)}
<h2>证据</h2><p><a href="summary.json">汇总及完整新/消失框</a> · <a href="replay-manifest.json">新源码清单</a> · <a href="{baseline_link}">v2源码清单</a> · <a href="{raw_link}">同期截图和native原件</a></p>{data_links}{table(checks)}</html>'''
    (output/'index.html').write_text(page, encoding='utf-8')
    (output/'README.md').write_text(title+'\n\n'+description+'\n\n'+exposure+' '+args.context_note+'\n\n同期Element typed native固定，生产旧/新CV+fusion/rank配对；同目标三点相关，不与历史开发集混合。\n', encoding='utf-8')
    print(description)
    print(json.dumps(transition_summary, ensure_ascii=False))


if __name__ == '__main__':
    main()
