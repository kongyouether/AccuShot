"""Copy the approved SVG geometry and render Windows application icon sizes locally."""
import json
import os
from pathlib import Path
import shutil
import xml.etree.ElementTree as ET

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")
from PIL import Image
from PyQt5.QtCore import QByteArray
from PyQt5.QtGui import QGuiApplication, QImage, QPainter
from PyQt5.QtSvg import QSvgRenderer

root = Path(__file__).resolve().parent
reference = root.parent / "docs/ui-directions/weijie"
icons = json.loads((reference / "icons.js").read_text(encoding="utf-8").split("=", 1)[1].strip().rstrip(";"))
destination = root / "icons"
destination.mkdir(exist_ok=True)
for name, svg in icons.items():
    ET.fromstring(svg)
    (destination / f"{name}.svg").write_text(svg + "\n", encoding="utf-8")
shutil.copyfile(reference / "assets/app.svg", root / "app.svg")

qrc_path = root / "resources.qrc"
qrc = ET.parse(qrc_path)
group = qrc.getroot().find("qresource[@prefix='/icons']")
for element in list(group):
    if element.attrib.get("alias", "").startswith("ui/") or element.attrib.get("alias") == "app.svg":
        group.remove(element)
ET.SubElement(group, "file", alias="app.svg").text = "app.svg"
for name in sorted(icons):
    ET.SubElement(group, "file", alias=f"ui/{name}.svg").text = f"icons/{name}.svg"
ET.indent(qrc, space="    ")
qrc.write(qrc_path, encoding="unicode")
with qrc_path.open("a", encoding="utf-8") as stream:
    stream.write("\n")

app = QGuiApplication([])
renderer = QSvgRenderer(QByteArray((root / "app.svg").read_bytes()))
assert renderer.isValid()
sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
images = []
for size in sizes:
    image = QImage(size, size, QImage.Format_ARGB32)
    image.fill(0)
    painter = QPainter(image)
    renderer.render(painter)
    painter.end()
    data = image.convertToFormat(QImage.Format_RGBA8888)
    bits = data.bits()
    bits.setsize(data.byteCount())
    images.append(Image.frombytes("RGBA", (size, size), bytes(bits)))
    if size == 256:
        assert image.save(str(root / "app_icon_256.png"))
images[-1].save(root / "app.ico", sizes=[(s, s) for s in sizes], append_images=images[:-1])
assert len(Image.open(root / "app.ico").ico.sizes()) == len(sizes)

# Native QSS arrows use raster URLs; render them from the same rounded 24-unit grid.
paths = {"down": "M7 10L12 15L17 10", "up": "M7 14L12 9L17 14", "check": "M5 12L10 17L19 7"}
for variant, color in {"light": "#1F3932", "dark": "#E4F1E8", "white": "#F3FAF5"}.items():
    for name, path in paths.items():
        ink = "#102D25" if name == "check" and variant == "light" else color
        svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24">'
               f'<path d="{path}" fill="none" stroke="{ink}" stroke-width="2" '
               'stroke-linecap="round" stroke-linejoin="round"/></svg>')
        image = QImage(48, 48, QImage.Format_ARGB32)
        image.fill(0)
        painter = QPainter(image)
        QSvgRenderer(QByteArray(svg.encode())).render(painter)
        painter.end()
        assert image.save(str(root / f"ui/{name}-{variant}.png"))
print(f"Copied {len(icons)} approved vectors; rendered {len(sizes)} application icon sizes.")
