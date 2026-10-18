"""Synthetic local-only screenshots; measure the production Qt converter process tree."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import psutil
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "docs/evidence/markdown-2026-10-18"

def samples():
    font = ImageFont.truetype("C:/Windows/Fonts/msyh.ttc", 26)
    title = ImageFont.truetype("C:/Windows/Fonts/msyhbd.ttc", 36)
    code = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 26)
    paths = []
    for kind in ("text", "table", "mixed"):
        image = Image.new("RGB", (1000, 720), "white")
        draw = ImageDraw.Draw(image)
        draw.text((45, 35), "Markdown 转换测试", font=title, fill="black")
        draw.text((45, 105), "这是一张人工生成的截图，用于离线验收。", font=font, fill="black")
        if kind == "text":
            for i, line in enumerate(["第一段：保留中文、English 和数字 12345。", "第二段：识别完成后，请检查原文。", "1. 安装插件", "2. 选择截图", "3. 保存 Markdown"]):
                draw.text((45, 180+i*60), line, font=font, fill="black")
        elif kind == "table":
            for y in (200, 280, 360, 440):
                draw.line((45, y, 850, y), fill="black", width=2)
            for x in (45, 450, 850):
                draw.line((x, 200, x, 440), fill="black", width=2)
            for i, row in enumerate([("项目", "数量"), ("Alpha", "12"), ("Beta", "34")]):
                for x, value in zip((70, 475), row):
                    draw.text((x, 225+i*80), value, font=font, fill="black")
        else:
            draw.text((45, 190), "公式：", font=font, fill="black")
            draw.text((90, 245), "x + y = 1", font=title, fill="black")
            for i, line in enumerate(["int total = 0;", "for (int i = 0; i < 3; ++i) {", "    total += i;", "}"]):
                draw.text((45, 355+i*45), line, font=code, fill="black")
        path = OUT / f"sample-{kind}.png"
        image.save(path)
        paths.append(path)
    return paths

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--provider", required=True)
    parser.add_argument("--exe", type=Path, required=True)
    args = parser.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    rows = []
    for path in samples():
        stem = f"{args.provider}-{path.stem}"
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORM_PLUGIN_PATH="C:/Qt/6.11.1/msvc2022_64/plugins/platforms", EVAN_PLUGIN_OFFLINE_TEST="1",
                   EVAN_MARKDOWN_LOCAL=args.provider, EVAN_MARKDOWN_IMAGE=str(path),
                   EVAN_MARKDOWN_OUTPUT=str(OUT / f"{stem}.md"))
        env["PATH"] = "C:/Qt/6.11.1/msvc2022_64/bin;" + env["PATH"]
        start = time.perf_counter()
        with (OUT / f"{stem}-process.log").open("w", encoding="utf-8") as log:
            proc = subprocess.Popen([str(args.exe.resolve()), "markdownRealLocal", "-o", str(OUT / f"{stem}.txt")+",txt"], cwd=ROOT, env=env, stdout=log, stderr=log)
            peak = 0
            while proc.poll() is None:
                try:
                    parent = psutil.Process(proc.pid)
                    rss = 0
                    for p in [parent, *parent.children(recursive=True)]:
                        try:
                            rss += p.memory_info().rss
                        except psutil.NoSuchProcess:
                            pass
                    peak = max(peak, rss)
                except psutil.NoSuchProcess:
                    pass
                time.sleep(0.1)
        row = {"provider": args.provider, "sample": path.name, "exit_code": proc.returncode,
               "wall_seconds": round(time.perf_counter()-start, 3), "peak_tree_rss_mib": round(peak/1024**2, 1)}
        rows.append(row)
        print(json.dumps(row), flush=True)
    (OUT / f"{args.provider}-benchmark.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
    raise SystemExit(1 if any(row["exit_code"] for row in rows) else 0)

if __name__ == "__main__":
    main()
