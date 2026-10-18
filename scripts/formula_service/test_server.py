import base64
import io
import json
import threading
import unittest
import urllib.request
import urllib.error
from http.server import HTTPServer
from PIL import Image
from server import handler_for

class BridgeTest(unittest.TestCase):
    def test_image_transport_and_invalid_request(self):
        seen = []
        def recognize(image):
            seen.append(image.size)
            return {"text": "x^2 + y^2 = 1"}
        server = HTTPServer(("127.0.0.1", 0), handler_for(recognize))
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            buffer = io.BytesIO()
            Image.new("RGB", (120, 40), "white").save(buffer, "PNG")
            url = f"http://127.0.0.1:{server.server_port}/recognize"
            data = json.dumps({"image": base64.b64encode(buffer.getvalue()).decode()}).encode()
            request = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
            with urllib.request.urlopen(request) as response:
                self.assertEqual(json.load(response)["latex"], "x^2 + y^2 = 1")
            self.assertEqual(seen, [(120, 40)])
            with self.assertRaises(urllib.error.HTTPError) as failure:
                urllib.request.urlopen(urllib.request.Request(url, data=b"{}"))
            self.assertEqual(failure.exception.code, 500)
        finally:
            server.shutdown()
            server.server_close()
            worker.join()

if __name__ == "__main__":
    unittest.main()
