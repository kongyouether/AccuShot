"""Render local fusion candidate evidence. No inference or labels are added."""
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
evidence = root / 'docs/evidence/smart-snap-fusion-2026-10-18'
data = json.loads((evidence / 'combined-candidates.json').read_text(encoding='utf-8'))
page = '''<!doctype html><meta charset="utf-8"><title>窗口 / UIA / CV 联合候选探针</title>
<style>body{font:15px system-ui;background:#19201e;color:#e8f2ed;margin:24px}button{margin:4px;padding:8px}#stage{position:relative;max-width:1400px}img,svg{width:100%;display:block}svg{position:absolute;inset:0;height:100%}pre{white-space:pre-wrap}label{margin-right:20px}</style>
<h1>窗口 / UIA / CV 联合候选探针</h1><p>本地只读探针。蓝框 UIA（含窗口），绿框 CV。候选数量并不代表命中率；静态快照与随后 UIA 查询可能有内容时差。</p>
<p><label><input id="ui" type="checkbox" checked>UIA</label><label><input id="cv" type="checkbox" checked>CV</label></p><div id="buttons"></div>
<div id="stage"><img src="fusion-current.png"><svg id="svg" viewBox="0 0 2560 1440"></svg></div><pre id="detail"></pre>
<script>const data=DATA;let index=0;const svg=document.getElementById('svg');const dpr=data.screens[0].dpr;
data.points.forEach((p,i)=>{const b=document.createElement('button');b.textContent=p.local_logical_point.join(', ');b.onclick=()=>{index=i;render()};buttons.append(b)});
function render(){const p=data.points[index];svg.innerHTML='';for(const [key,color] of [['ui','#369aff'],['cv','#38e18c']]){if(!document.getElementById(key).checked)continue;const bs=key==='ui'?p.element_trials[1].final_boxes:p.cv_boxes;bs.forEach((b,i)=>{const r=document.createElementNS('http://www.w3.org/2000/svg','rect');['x','y','width','height'].forEach((k,j)=>r.setAttribute(k,b[j]*dpr));r.setAttribute('fill','none');r.setAttribute('stroke',color);r.setAttribute('stroke-width',i===0?4:2);svg.append(r)})}const c=document.createElementNS('http://www.w3.org/2000/svg','circle');c.setAttribute('cx',p.physical_point[0]);c.setAttribute('cy',p.physical_point[1]);c.setAttribute('r',6);c.setAttribute('fill','#ff557f');svg.append(c);detail.textContent=JSON.stringify(p,null,2)}document.getElementById('ui').onchange=render;document.getElementById('cv').onchange=render;render();</script>'''
(evidence / 'index.html').write_text(page.replace('DATA', json.dumps(data, ensure_ascii=False)), encoding='utf-8')
