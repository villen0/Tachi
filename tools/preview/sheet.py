#!/usr/bin/env python3
"""Compose the PPM frames written by preview into labelled PNG contact sheets.
Usage: python3 sheet.py out_dir sheet_prefix"""
import sys, glob, os
from PIL import Image, ImageDraw
d, prefix = sys.argv[1], sys.argv[2]
SCALE = 2
def sheet(files, name, cols):
    imgs = [(os.path.basename(f)[:-4], Image.open(f).convert("RGB")) for f in files]
    if not imgs: return
    w, h = imgs[0][1].size
    cw, ch = w * SCALE + 8, h * SCALE + 26
    rows = (len(imgs) + cols - 1) // cols
    s = Image.new("RGB", (cols * cw, rows * ch), (24, 24, 28))
    dr = ImageDraw.Draw(s)
    for i, (n, im) in enumerate(imgs):
        x, y = (i % cols) * cw + 4, (i // cols) * ch + 2
        s.paste(im.resize((w * SCALE, h * SCALE), Image.NEAREST), (x, y + 20))
        dr.text((x + 2, y + 4), n, fill=(255, 230, 90))
    s.save(name)
    print(name, s.size)
ex = sorted(glob.glob(os.path.join(d, "e*.ppm")))
half = (len(ex) + 1) // 2
sheet(ex[:half], prefix + "_1.png", 4)
sheet(ex[half:], prefix + "_2.png", 4)
sheet(sorted(glob.glob(os.path.join(d, "t_*.ppm"))), prefix + "_trans.png", 6)
sheet(sorted(glob.glob(os.path.join(d, "b_*.ppm"))), prefix + "_blink.png", 6)
