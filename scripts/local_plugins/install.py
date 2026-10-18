"""Install an optional backend into its own directory; never mutate global Python."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import venv

PACKAGES = {
    "translation-en-zh": ["argostranslate==1.11.0"],
    "latex-ocr-engine": ["rapid-latex-ocr==0.0.9", "requests>=2.32,<3"],
    "pix2text": ["pix2text==1.1.7", "cnstd==1.2.6.1", "rapidocr==3.8.4"],
    "pix2text-page": ["pix2text==1.1.7", "cnstd==1.2.6.1", "rapidocr==3.8.4"],
    "paddleformula": ["paddlepaddle==3.2.2", "paddleocr==3.7.0", "tokenizers==0.22.2", "ftfy==6.3.1"],
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--id", choices=PACKAGES, required=True)
    parser.add_argument("--destination", type=Path, required=True)
    args = parser.parse_args()
    root = args.destination.resolve()
    root.mkdir(parents=True, exist_ok=True)
    marker = root / "installed.json"
    marker.unlink(missing_ok=True)
    python = root / "venv/Scripts/python.exe"
    if not python.exists():
        venv.EnvBuilder(with_pip=True).create(root / "venv")
    subprocess.run([str(python), "-m", "pip", "install", *PACKAGES[args.id]], check=True)
    shutil.copyfile(Path(__file__).with_name("runner.py"), root / "runner.py")
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
