"""Synthetic, non-private acceptance inputs; run with Pillow and matplotlib."""
import json
from pathlib import Path
import sys
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from PIL import Image, ImageDraw, ImageFont, ImageFilter

root = Path(sys.argv[1]).resolve()
root.mkdir(parents=True, exist_ok=True)
formulas = {
    "fraction": r"\frac{a+b}{c+d}",
    "powers": r"x_i^2+y_{i+1}^3=1",
    "root": r"\sqrt{x^2+y^2}",
    "integral": r"\int_0^1 x^2\,dx=\frac{1}{3}",
    "matrix": r"\left[\begin{matrix}1&2\\3&4\end{matrix}\right]",
    "multiline": r"x+y=3\\x-y=1",
}
for name, latex in formulas.items():
    fig = plt.figure(figsize=(5, 1.8), dpi=140)
    fig.patch.set_facecolor("white")
    if name == "matrix":
        for x,y,text in [(0.36,0.7,"1"),(0.54,0.7,"2"),(0.36,0.24,"3"),(0.54,0.24,"4")]:
            fig.text(x,y,f"${text}$",fontsize=30)
        fig.text(.24,.22,"[",fontsize=85);fig.text(.65,.22,"]",fontsize=85)
    elif name == "multiline":
        fig.text(.2,.65,r"$x+y=3$",fontsize=30);fig.text(.2,.2,r"$x-y=1$",fontsize=30)
    else:
        fig.text(.08,.35,f"${latex}$",fontsize=32)
    fig.savefig(root/f"{name}.png",facecolor="white");plt.close(fig)
image=Image.open(root/"fraction.png").convert("RGB")
image.resize((350,126)).filter(ImageFilter.GaussianBlur(.7)).save(root/"blur.png")
formulas["blur"]=formulas["fraction"]
Image.new("RGB",(500,160),"white").save(root/"blank.png")
formulas["blank"]=""
image=Image.new("RGB",(1100,260),"white");draw=ImageDraw.Draw(image)
font=ImageFont.truetype("C:/Windows/Fonts/msyh.ttc",36)
draw.text((30,25),"本地文字识别测试 2026",font=font,fill="black")
draw.text((30,105),"The quick brown fox jumps over the lazy dog.",font=font,fill="black")
draw.text((30,185),"Order: A-1024, total: 125.50",font=font,fill="black")
image.save(root/"ocr.png")
(root/"expected.json").write_text(json.dumps(formulas,ensure_ascii=False,indent=2),encoding="utf-8")
