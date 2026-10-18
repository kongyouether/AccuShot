"""Publish the local refinement evidence entry point without pooling unlike cohorts."""
import argparse
import html
import json
from html.parser import HTMLParser
from pathlib import Path

from hybrid_report import table, percentile, write_csv
from ranking_report import digest, load


def main():
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument('--evidence', type=Path, default=root/'docs/evidence/snap-refinement-2026-10-18')
    parser.add_argument('--history', default='history-v4')
    parser.add_argument('--regression', default='live-regression-stage3')
    parser.add_argument('--recheck', default='holdout-recheck-current')
    args = parser.parse_args()
    output = args.evidence.resolve()
    paths = [args.history, args.regression, args.recheck]
    manifests = [load(output/name/'replay-manifest.json') for name in paths]
    core_sha = manifests[0]['smart_snapper_cpp_sha256']
    assert all(m['smart_snapper_cpp_sha256'] == core_sha for m in manifests)
    assert digest(root/'src/core/smart_snapper.cpp') == core_sha
    datasets = [load(output/name/'summary.json') for name in paths]
    labels = ['历史开发集', '新现场已曝光修复回归', '先前留出集最终复核']
    metrics = ['top3', 'wheel_le2', 'any', 'top1']
    rows = []
    category_rows = []
    for index, (label, name, data) in enumerate(zip(labels, paths, datasets)):
        before, after = data['summary'][:2]
        rows.append(dict(分层=label, 目标=before.get('targets', before['points']), 点=before['points'],
                         **{metric: f'{before[metric+"_hits"]} → {after[metric+"_hits"]}' for metric in metrics}))
        if index:
            for a, b in zip(data['summary'][2::2], data['summary'][3::2]):
                category_rows.append(dict(分层=label, 界面类别=a['category'], 目标=a['targets'], 点=a['points'],
                                          **{metric: f'{a[metric+"_hits"]} → {b[metric+"_hits"]}' for metric in metrics}))
    history_timing = [r for r in datasets[0]['timings'] if r['scene'] == 'all']
    animation = load(output/'animation/paint-timing.json')
    deployment = load(output/'deployment.json')
    assert deployment['core_sha256'].lower() == core_sha
    live_points = []
    for scene in ('web-codex-stable', 'paper', 'explorer'):
        capture = load(output/'live'/(scene+'.json'))
        for point in capture['points']:
            for mode in ('Window', 'Element', 'Hybrid'):
                measured = point['modes'][mode]
                first_uia = next((change['at_ms'] for change in measured['changes']
                                  if any(c['sources'] & 2 for c in change.get('details', []))), None)
                live_points.append(dict(scene=scene, category=point['category'], id=point['id'], mode=mode,
                                        first_recorded_uia_ms=first_uia, first_candidate_ms=measured['first_candidate_ms'],
                                        ready_ms=measured['ready_ms'], query_ms=measured['query_ms'],
                                        poll_ms=capture['poll_ms'], capture_utc=capture['capture_utc']))
    live_timing = []
    for category in ('codex', 'web', 'paper', 'explorer'):
        for mode in ('Window', 'Element', 'Hybrid'):
            measured = [p for p in live_points if p['category'] == category and p['mode'] == mode]
            first = [p['first_recorded_uia_ms'] for p in measured if p['first_recorded_uia_ms'] is not None]
            queries = [t for p in measured for t in p['query_ms']]
            live_timing.append(dict(category=category, mode=mode, points=len(measured), UIA_observed_points=len(first),
                                    UIA_first_visible_p50_ms=percentile(first, .5), UIA_first_visible_p95_ms=percentile(first, .95),
                                    ready_p95_ms=percentile([p['ready_ms'] for p in measured], .95),
                                    sync_query_samples=len(queries), sync_query_p50_ms=percentile(queries, .5),
                                    sync_query_p95_ms=percentile(queries, .95), sync_query_max_ms=max(queries)))
    cv_comparison = []
    for scene, old_dir, new_dir in [('web-codex-stable', 'holdout-stage1-baseline', args.regression),
                                     ('paper', 'holdout-final-baseline', args.recheck),
                                     ('explorer', 'holdout-final-baseline', args.recheck)]:
        before = load(output/old_dir/(scene+'-replay.json'))['preprocess_ms'][1:]
        after = load(output/new_dir/(scene+'-replay.json'))['preprocess_ms'][1:]
        cv_comparison.append(dict(capture=scene, warm_runs_per_version=len(before),
                                  v2_CV_p50_ms=percentile(before, .5), final_CV_p50_ms=percentile(after, .5),
                                  v2_CV_p95_ms=percentile(before, .95), final_CV_p95_ms=percentile(after, .95)))
    write_csv(output/'live-timings.csv', live_timing)
    write_csv(output/'new-image-cv-comparison.csv', cv_comparison)
    (output/'live-timings.json').write_text(json.dumps(dict(
        scope='single live capture per scene; no old/new UIA paired benchmark; first recorded SourceElement change',
        points=live_points, summary=live_timing, new_image_cv_comparison=cv_comparison), ensure_ascii=False, indent=2), encoding='utf-8')
    links = ''.join(f'<li><a href="{name}/index.html">{label}完整报告、全部候选和图像</a></li>' for label, name in zip(labels, paths))
    failure_rows = []
    for label, name in zip(labels, paths):
        import csv
        with (output/name/'points.csv').open(encoding='utf-8-sig') as handle:
            for row in csv.DictReader(handle):
                if row['version'] == 'after' and row['any'] == 'False':
                    failure_rows.append(dict(分层=label, 界面=row['scene'], 目标=row['name'], 点=row['id'], 最大IoU=row['any_iou']))
    page = f'''<!doctype html><html lang="zh-CN"><meta charset="utf-8"><title>轮廓细化与候选切换：证据总入口</title>
<style>body{{font:16px system-ui;max-width:1200px;margin:36px auto;padding:0 24px;color:#18332c;background:#f4f8f6}}p,li{{line-height:1.75}}a{{color:#176d56}}table{{border-collapse:collapse;background:white;font-size:14px}}th,td{{padding:9px;border:1px solid #ccdcd5;white-space:nowrap}}.scroll{{overflow:auto}}code{{overflow-wrap:anywhere}}img{{max-width:100%}}</style>
<h1>轮廓细化与候选切换：证据总入口</h1>
<p>最终生产core SHA256：<code>{core_sha}</code>。基线为部署过的ranking v2（冻结源码SHA256 <code>C0AC60B1EE685498E00A96D6D75BCAD75F2322FD1CC428B4CC3A157DD2E8A519</code>）。三个最终报告均校验为同一生产core版本。</p>
<h2>构建、部署与测试</h2><p>Release增量构建后已备份旧EXE/PDB、替换并启动。部署记录中的EXE SHA256：<code>{deployment['sha256']}</code>，PID {deployment['pid']}；原偏好{deployment['snapping_mode']}保留。启动日志确认初始化及F1/F3热键注册。部署core哈希与三组最终报告一致。</p>
<p><a href="deployment.json">部署、备份与哈希</a> · <a href="startup.log">启动日志</a> · <a href="fusion-tests-final.txt">fusion 44/44</a> · <a href="cv-tests-final.txt">CV 20/20</a> · <a href="ui-tests-final.txt">Windows QtTest 8 passed</a>（6项业务方法+初始化/清理；0失败0跳过）。</p>
<h2>结果分层</h2><p>IoU≥0.8；箭头为v2→最终版本。Top3为默认推荐前三项；wheel_le2为从默认框按稳定面积排序双向滚轮≤2步可达；any为完整候选至少一个命中。各分层独立统计。</p>{table(rows)}
<p>历史20点来自已有20个目标；新现场为15个目标、每目标3个相关鼠标点。三个点不能当作三个独立目标，也不能把新现场45点与历史开发集混成一个独立准确率。新现场Web/Codex用于修复过排序，属于回归集。paper/Explorer首次留出结果另存；最终版本复核时其汇总已知，未针对这些目标改动。</p>
<h2>新现场四类界面</h2>{table(category_rows)}
<p>论文使用同一篇论文的新标题页和响应式布局，Explorer切换到build-release目录；属于场景扩展，未证明跨论文普遍泛化。新留出与最终复核均未见准确率增益。全部失败目标和每个鼠标点原样保留。</p>
<ul>{links}<li><a href="holdout-final-current/index.html">paper/Explorer首次留出原报告（当时core BE60AD…）</a></li>
<li><a href="holdout-stage1-current/index.html">Web/Codex首次暴露的真实失败</a> · <a href="live-regression-stage2/index.html">stage2中间回归</a> · <a href="stage2-diagnosis.md">stage2退化诊断</a></li>
<li><a href="live/">有效现场原件与预先标注</a> · <a href="live/INVALID-web-codex.txt">自动滚动作废组说明</a> · <a href="baseline-source/smart_snapper-v2.cpp">冻结v2生产源码</a></li></ul>
<h2>范围与可复现性</h2><p>历史固定PNG和GT只读，重新运行生产processSnapshot，经friend测试接口调用生产fuseCandidates，未复制算法。历史native来自旧Hybrid已去重结果，去掉CV来源位；无controlType，始终未知，存在旧几何选择偏差，不能宣称历史语义改善。新现场两版本固定使用同期Element typed native，必要时从Window补owner；离线重放均不访问当前UIA。</p>
<p>所有新/消失框、来源、深度、controlType、owner与请求身份保存在各组逐场景JSON；原图、GT、原始数据与基线以SHA256核对。当前语义收敛保留完整Text段落几何顺序，仅有明确文本子项的控件增加完整层级证据；新增行框要求逻辑高度≥24，其余小框保留。历史开发改进不扩展为全场景已解决。</p>
<h2>最终历史开发集耗时</h2>{table(history_timing)}
<p>CV每图21次，第1次单列，其余20次热分布；查询/融合各点1000次。计时排除图片解码、当前UIA、GUI输入与绘制。极短查询易受计时器分辨率影响，原始精度在JSON。</p>
<h2>新图旧版/最终版CV耗时对比</h2>{table(cv_comparison)}
<p>同PNG各版本21次，第1次剔除，20次热分布。仅processSnapshot；两版依次离线运行，未隔离后台负载，因此不能把小幅差值当作稳定速度收益。<a href="new-image-cv-comparison.csv">CSV</a></p>
<h2>现场原生/UIA延迟与同步查询</h2><p>以下直接汇总live三份有效原始JSON。每场景只采集一次，每点新建探针实例；不是UIA前后版本同测，也不是最终部署程序的全GUI延迟。UIA_first_visible从changes中首次记录sources含Element位的at_ms计算；ready为控件查询完成观测；Window模式没有UIA观测。异步UIA可在轮询间隙完成，数字包含轮询量化，且模式计时在该模式CV前处理之后才开始。不能把first_candidate_ms的早期Window/CV框当作UIA结果。</p>
{table(live_timing)}<p><strong>Explorer的Element/Hybrid各12点均未记录到Element来源候选，本次依靠Window/CV，不能当作Explorer元素融合通过。</strong>首次可见列为空，不能理解为0ms；约26ms的ready仅表示查询已完成，不代表UIA成功。该场景留待下一轮诊断，原始采集未补造或覆盖。sync_query是findCandidateDetailsAt同步调用墙钟耗时，包含同步原生查询及缓存/融合开销，异步UIA等待由首次可见和ready另报。样本是同一次界面采集内的相关查询，不是独立多轮基准。<a href="live-timings.json">逐点首次可见与原始查询数组</a> · <a href="live-timings.csv">汇总CSV</a></p>
<h2>候选切换动画绘制</h2><p>生产SnippingOverlay::paintEvent经QWidget::grab，在Windows平台640×480逻辑画布、DPR {animation['devicePixelRatio']}、350帧合成候选中，P50 {animation['renderP50Ms']}ms，P95 {animation['renderP95Ms']}ms。动画时长90ms。此数值仅为grab/render，不含屏幕合成或输入，不是端到端FPS。</p>
<p><a href="animation/paint-timing.json">绘制原始计时</a> · <a href="animation/paint-timing-test.txt">QtTest原件</a> · <a href="animation/02-switch-045ms.png">中间帧</a> · <a href="animation/04-click-exact-target.png">点击精确目标帧</a></p>
<h2>最终仍无命中候选的点</h2>{table(failure_rows)}
<p><a href="validation.json">报告引用与版本核验</a>。更早探索版本：<a href="history-v1/index.html">history-v1</a> · <a href="history-v2/index.html">history-v2</a> · <a href="history-v3/index.html">history-v3</a>。</p></html>'''
    (output/'index.html').write_text(page, encoding='utf-8')
    (output/'README.md').write_text('# 轮廓细化与候选切换证据\n\n入口：[index.html](index.html)。最终core SHA256：'+core_sha+
        '\n\n分层：历史开发20目标20点；新现场回归Web/Codex 7目标21相关点；先前留出最终复核paper/Explorer 8目标24相关点。不可合并为独立样本总体准确率。\n\n'+
        '\n'.join('- '+str(row) for row in rows)+'\n\n首次暴露失败、中间回归、首次留出与全部候选/GT/PNG原件均保留。复现工具见 tests/contour/refinement_replay.cpp、refinement_report.py、refinement_holdout_report.py、refinement_index.py。独立CMake入口 tests/contour/refinement；评测独占build-snap-refinement-eval和build-snap-refinement-baseline。\n\n最终产品由主线完成Release构建及部署：[deployment.json](deployment.json)、[startup.log](startup.log)。PID '+str(deployment['pid'])+'，EXE SHA256 '+deployment['sha256']+'，Hybrid偏好保留。[fusion 44/44](fusion-tests-final.txt)、[CV 20/20](cv-tests-final.txt)、[Windows QtTest 8 passed](ui-tests-final.txt)（6业务+初始化/清理）。\n\n现场UIA与同步查询单次采集时耗：[live-timings.json](live-timings.json)、[live-timings.csv](live-timings.csv)；新图旧/新CV对比：[new-image-cv-comparison.csv](new-image-cv-comparison.csv)。单次采集不是UIA前后版本同测；Explorer未记录Element来源候选，空白不表示0ms。\n', encoding='utf-8')
    references, missing = [], []
    class Links(HTMLParser):
        def handle_starttag(self, tag, attributes):
            for key, value in attributes:
                if key in ('src', 'href') and not value.startswith(('http:', 'https:', '#')):
                    references.append((current, value))
    for current in [output/'index.html']+[output/name/'index.html' for name in paths]:
        Links().feed(current.read_text(encoding='utf-8'))
    for path, value in references:
        if value == 'validation.json':
            continue
        if not (path.parent/value).exists():
            missing.append(str(path.relative_to(output))+':'+value)
    assert not missing, missing
    validation = dict(final_core_sha256=core_sha, three_final_reports_same_core=True,
                      deployment_core_matches_final_reports=True, deployment_exe_sha256=deployment['sha256'],
                      deployment_pid=deployment['pid'],
                      local_references_checked=len(references), missing=missing,
                      historical_gt_png_json_and_v2_hash_checks='passed by report',
                      live_same_native_gt_png_json_checks='passed by report',
                      visual_review=['historical Explorer folder row', 'historical paper phone subfigure',
                                     'live Codex chat row', 'live paper Abstract heading failure', 'live Explorer File list pane failure'],
                      cohorts_not_pooled=True)
    (output/'validation.json').write_text(json.dumps(validation, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps(validation, ensure_ascii=False))


if __name__ == '__main__':
    main()
