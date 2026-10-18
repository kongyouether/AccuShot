"""Local-only formula bridge. Start one independently installed OCR backend."""
import argparse
import base64
import io
import json
from http.server import BaseHTTPRequestHandler, HTTPServer


def create_engine(backend, model):
    if backend == "pix2text":
        from pix2text.latex_ocr import LatexOCR
        engine = LatexOCR(device="cpu")
        return lambda image: engine(image)
    from paddleocr import FormulaRecognition
    import numpy as np
    engine = FormulaRecognition(model_name=model, device="cpu")
    return lambda image: next(iter(engine.predict(input=np.asarray(image))))["rec_formula"]


def handler_for(recognize):
    class Handler(BaseHTTPRequestHandler):
        def do_POST(self):
            if self.path != "/recognize":
                self.send_error(404)
                return
            try:
                from PIL import Image
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= 16 * 1024 * 1024:
                    self.send_error(413)
                    return
                payload = json.loads(self.rfile.read(size))
                image = Image.open(io.BytesIO(base64.b64decode(payload["image"], validate=True))).convert("RGB")
                result = recognize(image)
                if isinstance(result, dict):
                    result = result.get("text", "")
                if not isinstance(result, str) or not result.strip():
                    raise ValueError("empty recognition result")
                self.respond(200, {"latex": result})
            except Exception as error:
                # Do not echo request images or private formulas into logs.
                self.respond(500, {"error": type(error).__name__})

        def respond(self, status, value):
            data = json.dumps(value, ensure_ascii=False).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def log_message(self, *_):
            pass
    return Handler


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--backend", choices=["pix2text", "paddle"], required=True)
    parser.add_argument("--port", type=int)
    parser.add_argument("--model", default="PP-FormulaNet_plus-M")
    args = parser.parse_args()
    engine = create_engine(args.backend, args.model)
    port = args.port or (8787 if args.backend == "pix2text" else 8788)
    server = HTTPServer(("127.0.0.1", port), handler_for(engine))
    print(f"Formula service ready on localhost:{port}", flush=True)
    server.serve_forever()
