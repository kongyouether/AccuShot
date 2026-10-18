"""Plugin process protocol: JSON request/result files; inference uses local weights."""
import argparse
import json
import os
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parent
# CnSTD's model downloader invokes huggingface-cli; use this plugin's executable.
os.environ["PATH"] = str(Path(sys.executable).parent) + os.pathsep + os.environ.get("PATH", "")
# Download helpers print Unicode warnings even when launched with redirected pipes on Windows.
os.environ["PYTHONUTF8"] = "1"
os.environ["PYTHONIOENCODING"] = "utf-8"
# All downloaded model state belongs to this plugin and is removed with it.
os.environ["HF_HOME"] = str(ROOT / "models/huggingface")
os.environ["XDG_CACHE_HOME"] = str(ROOT / "models/cache")
os.environ["XDG_DATA_HOME"] = str(ROOT / "models/data")
os.environ["XDG_CONFIG_HOME"] = str(ROOT / "models/config")
os.environ["ARGOS_PACKAGES_DIR"] = str(ROOT / "models/argos")
os.environ["PADDLE_PDX_CACHE_HOME"] = str(ROOT / "models/paddle")
os.environ["PIX2TEXT_HOME"] = str(ROOT / "models/pix2text")
os.environ["CNOCR_HOME"] = str(ROOT / "models/cnocr")
os.environ["CNSTD_HOME"] = str(ROOT / "models/cnstd")
os.environ["YOLO_CONFIG_DIR"] = str(ROOT / "models/ultralytics")
os.environ["YOLO_OFFLINE"] = "true"
os.environ["PADDLE_PDX_DISABLE_MODEL_SOURCE_CHECK"] = "True"


def load(backend, prepare):
    if backend == "translation-en-zh":
        import argostranslate.package as package
        import ctranslate2
        import re
        if prepare:
            package.update_package_index()
            for source, target in [("en", "zh"), ("zh", "en")]:
                if not any(p.from_code == source and p.to_code == target for p in package.get_installed_packages()):
                    p = next(p for p in package.get_available_packages() if p.from_code == source and p.to_code == target)
                    package.install_from_path(p.download())
        def run(request):
            source = request.get("source", "auto")
            if source == "auto":
                source = "zh" if any("\u4e00" <= c <= "\u9fff" for c in request["text"]) else "en"
            target = request["target"]
            if source not in ("en", "zh") or target not in ("en", "zh"):
                raise ValueError("This plugin supports Chinese and English only")
            if source == target:
                return request["text"]
            pkg = next(p for p in package.get_installed_packages() if p.from_code == source and p.to_code == target)
            model = ctranslate2.Translator(str(pkg.package_path / "model"), device="cpu", compute_type="int8", intra_threads=4)
            # Use the package tokenizer directly: Argos 1.11's Stanza path updates resources online
            # and its current bundled 1.9 resources are incompatible with Stanza 1.10.
            paragraphs = []
            for paragraph in request["text"].split("\n"):
                sentences = [v for v in re.split(r"(?<=[。！？.!?])(?=\s|$)", paragraph) if v.strip()]
                if not sentences:
                    paragraphs.append("")
                    continue
                tokens = [pkg.tokenizer.encode(v.strip()) for v in sentences]
                results = model.translate_batch(tokens, beam_size=4, length_penalty=0.2, replace_unknowns=True, max_input_length=0)
                paragraphs.append(("" if target == "zh" else " ").join(pkg.tokenizer.decode(r.hypotheses[0]).strip() for r in results))
            return "\n".join(paragraphs)

        return run
    from PIL import Image
    if backend == "pix2text-page":
        from pix2text import Pix2Text
        import rapidocr
        import tempfile
        # Reuse the pinned RapidOCR package's Chinese/English models. Pix2Text's
        # default densenet recognizer lost text on our screenshot samples.
        ocr_models = Path(rapidocr.__file__).parent / "models"
        text_config = {
            "det_model_name": "ch_PP-OCRv4_det",
            "det_model_fp": str(ocr_models / "ch_PP-OCRv4_det_mobile.onnx"),
            "rec_model_name": "ch_PP-OCRv4",
            "rec_model_fp": str(ocr_models / "ch_PP-OCRv4_rec_mobile.onnx"),
            # CnOCR omits font_path, required by RapidOCR 3.8.4's result object.
            "rec_more_configs": {"font_path": str(Path(os.environ["WINDIR"]) / "Fonts/msyh.ttc")},
        }
        model = Pix2Text.from_config({"text_formula": {"text": text_config}}, enable_table=True, enable_spell_checker=False, device="cpu")
        def run_page(request):
            page = model.recognize_page(request["image"], table_as_image=False, text_contain_formula=False)
            if "output_dir" in request:
                return page.to_markdown(request["output_dir"], root_url="assets", markdown_fn=None).replace("assets/figures\\", "assets/figures/")
            with tempfile.TemporaryDirectory() as output:
                return page.to_markdown(output, markdown_fn=None)
        return run_page
    if backend == "latex-ocr-engine":
        from rapid_latex_ocr import LaTeXOCR
        model = LaTeXOCR()
        return lambda request: model(request["image"])[0]
    if backend == "pix2text":
        from pix2text.latex_ocr import LatexOCR
        model = LatexOCR(device="cpu", root=str(ROOT / "models/pix2text"))
        return lambda request: model(Image.open(request["image"]).convert("RGB"))
    from paddleocr import FormulaRecognition
    model = FormulaRecognition(model_name="PP-FormulaNet_plus-M", device="cpu")
    return lambda request: next(iter(model.predict(input=request["image"])))['rec_formula']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", required=True)
    parser.add_argument("--prepare", action="store_true")
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if not args.prepare:
        os.environ["HF_HUB_OFFLINE"] = "1"
        os.environ["TRANSFORMERS_OFFLINE"] = "1"
        if args.backend == "pix2text-page" or os.environ.get("EVAN_PLUGIN_OFFLINE_TEST") == "1":
            def reject_network(event, _args):
                if event in ("socket.connect", "socket.getaddrinfo"):
                    # DocLayout-YOLO probes connectivity at import and handles OSError.
                    # Deny the socket before it is opened while preserving offline detection.
                    raise OSError("Offline plugin blocked a network attempt: " + event)
            sys.addaudithook(reject_network)
    start = time.perf_counter()
    try:
        request = None if args.prepare or args.verify else json.loads(args.input.read_text(encoding="utf-8"))
        if request is not None and "image" in request:
            from PIL import Image, ImageStat
            if ImageStat.Stat(Image.open(request["image"]).convert("L")).stddev[0] < 1:
                raise ValueError("图片为空白，未检测到内容")
        run = load(args.backend, args.prepare)
        if args.prepare or args.verify:
            if args.backend == "translation-en-zh":
                for source, target, text in [("en", "zh", "Hello world."), ("zh", "en", "你好世界。")]:
                    if not run({"text": text, "source": source, "target": target}):
                        raise ValueError("Translation model smoke test returned no text")
            else:
                from PIL import Image, ImageDraw, ImageFont
                sample = ROOT / "install-sample.png"
                image = Image.new("RGB", (400, 120), "white")
                font = ImageFont.truetype("C:/Windows/Fonts/cambria.ttc", 40)
                ImageDraw.Draw(image).text((20, 25), "x + y = 1", font=font, fill="black")
                if args.backend == "pix2text-page":
                    image = Image.new("RGB", (900, 600), "white")
                    draw = ImageDraw.Draw(image)
                    for y, text in [(35, "Document example"), (130, "Local recognition test."), (230, "x + y = 1"), (350, "This document contains text and a formula.")]:
                        draw.text((30, y), text, font=font, fill="black")
                image.save(sample)
                try:
                    recognized = run({"image": str(sample)})
                    if not recognized:
                        raise ValueError("Formula model smoke test returned no text")
                    if args.backend == "pix2text-page" and not all(word in recognized.lower() for word in ("document", "local")):
                        raise ValueError("Document model smoke test did not recover the expected sample words")
                finally:
                    sample.unlink(missing_ok=True)
            return
        text = run(request)
        if isinstance(text, dict):
            text = text.get("text", "")
        if not isinstance(text, str) or not text.strip():
            raise ValueError("The model returned an empty result")
        result = {"success": True, "text": text, "elapsed_ms": round((time.perf_counter()-start)*1000)}
    except Exception as error:
        if args.prepare or args.verify:
            raise
        import traceback
        traceback.print_exc()
        result = {"success": False, "error": str(error)}
    args.output.write_text(json.dumps(result, ensure_ascii=False), encoding="utf-8")
    if not result["success"]:
        sys.exit(1)


if __name__ == "__main__":
    main()
