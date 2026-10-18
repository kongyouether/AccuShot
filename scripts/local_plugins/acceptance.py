"""Exercise production C++ managers, retaining each input, output and duration."""
import argparse
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser()
parser.add_argument("--backend",required=True)
parser.add_argument("--root",type=Path,default=Path("build-local-models"))
args=parser.parse_args()
root=args.root.resolve();samples=root/"samples";results=root/"results";results.mkdir(exist_ok=True)
backend=args.backend
if backend in ("RapidOCR","WindowsMedia"):
    cases=[(name,{"image":str(samples/f"{name}.png")}) for name in ("ocr","blank")]
elif backend=="translation-en-zh":
    cases=[("en-zh",{"text":"Please save the document before closing the window.","source":"en","target":"zh"}),
           ("zh-en",{"text":"请在关闭窗口之前保存文档。","source":"zh","target":"en"}),
           ("paragraph",{"text":"The total price is 125.50 yuan.\nOrder number: A-1024.","source":"auto","target":"zh"}),
           ("unsupported",{"text":"Bonjour","source":"fr","target":"zh"})]
else:
    cases=[(name,{"image":str(samples/f"{name}.png")}) for name in json.loads((samples/"expected.json").read_text(encoding="utf-8"))]
env=os.environ.copy();env["EVAN_PLUGIN_OFFLINE_TEST"]="1";env["QT_QPA_PLATFORM"]="offscreen"
env["QT_QPA_PLATFORM_PLUGIN_PATH"]="C:/Qt/6.11.1/msvc2022_64/plugins/platforms"
env["PATH"]="C:/Qt/6.11.1/msvc2022_64/bin;"+env["PATH"]
exe=Path("build-release/local_probe.exe").resolve()
for name,request in cases:
    prefix=results/f"{backend.replace(':','-')}-{name}"
    request_path=prefix.with_suffix(".input.json");result_path=prefix.with_suffix(".json")
    request_path.write_text(json.dumps(request,ensure_ascii=False),encoding="utf-8")
    with prefix.with_suffix(".log").open("w",encoding="utf-8") as log:
        result=subprocess.run([str(exe),backend,str(request_path),str(result_path)],env=env,stdout=log,stderr=log,timeout=210)
    print(name,result.returncode,result_path.read_text(encoding="utf-8") if result_path.exists() else "No result",flush=True)
