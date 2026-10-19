"""Install an optional backend into its own directory; never mutate global Python."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import venv


def stage(message):
    print("ACCUSHOT_STAGE:" + message, flush=True)

PACKAGES = {
    "translation-en-zh": ["argostranslate==1.11.0"],
    "latex-ocr-engine": ["rapid-latex-ocr==0.0.9", "requests>=2.32,<3"],
    "pix2text": ["pix2text==1.1.7", "cnstd==1.2.6.1", "rapidocr==3.8.4"],
    "pix2text-page": ["pix2text==1.1.7", "cnstd==1.2.6.1", "rapidocr==3.8.4"],
    "paddleformula": ["paddlepaddle==3.2.2", "paddleocr==3.7.0", "tokenizers==0.22.2", "ftfy==6.3.1"],
}


def prepare_environment(root):
    python = root / "venv/Scripts/python.exe"
    stage("创建并验证插件独立环境")
    if not python.exists():
        venv.EnvBuilder(with_pip=True).create(root / "venv")
    # A stale venv must not silently keep using a different base interpreter.
    probe = subprocess.run([str(python), "-I", "-c",
        "import sys,ssl,struct,json; print(json.dumps([sys.base_prefix,list(sys.version_info[:2]),struct.calcsize('P')*8]))"],
        check=True, capture_output=True, text=True, encoding="utf-8")
    base, version, bits = json.loads(probe.stdout)
    if Path(base).resolve() != Path(sys.base_prefix).resolve() or version != [3, 12] or bits != 64:
        raise RuntimeError("插件环境与所选 Python 3.12 不一致，请卸载此插件后重新安装")
    subprocess.run([str(python), "-I", "-m", "pip", "--version"], check=True)
    return python


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--id", choices=PACKAGES, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    parser.add_argument("--runtime-config", type=Path)
    args = parser.parse_args()
    root = args.destination.resolve()
    root.mkdir(parents=True, exist_ok=True)
    marker = root / "installed.json"
    marker.unlink(missing_ok=True)
    python = prepare_environment(root)
    runtime = json.loads(args.runtime_config.read_text(encoding="utf-8")) if args.runtime_config else {
        "executable": sys.executable, "version": ".".join(map(str, sys.version_info[:3])), "conda": "", "prefix": ""}
    (root / "runtime.json").write_text(json.dumps(runtime), encoding="utf-8")
    stage("安装插件依赖")
    subprocess.run([str(python), "-I", "-m", "pip", "install", *PACKAGES[args.id]], check=True)
    source = Path(__file__).with_name("runner.py")
    if source.resolve() != (root / "runner.py").resolve():
        shutil.copyfile(source, root / "runner.py")
    stage("下载模型并验证初始化")
    subprocess.run([str(python), str(root / "runner.py"), "--backend", args.id, "--prepare"], check=True)
    if args.id == "pix2text-page":
        subprocess.run([str(python), str(root / "runner.py"), "--backend", args.id, "--verify"], check=True)
    weights = [str(p.relative_to(root)).replace("\\", "/") for p in root.rglob("*")
               if p.is_file() and (p.suffix in (".onnx", ".safetensors", ".pdiparams", ".pt", ".pth") or p.name == "model.bin") and p.stat().st_size > 1024 * 1024]
    if not weights:
        raise RuntimeError("No installed model weights found")
    marker.write_text(json.dumps({"id": args.id, "packages": PACKAGES[args.id], "version": 1, "weights": weights}), encoding="utf-8")
    print("Installed and model initialization passed", flush=True)


if __name__ == "__main__":
    main()
