#!/usr/bin/env python3
"""Glyph renderer shared by the font generators: anti-aliased 8-bit alpha bitmaps of Krona One (SIL OFL).
Used by fontgen_round.py, which writes src/round_font.h. Each class has a uniform cell height = digit cap height."""
import sys
from PIL import Image, ImageDraw, ImageFont

SS = 4  # supersampling factor

def fit_size(path, cap):
    lo, hi = 6, 400
    while lo < hi:
        mid = (lo + hi) // 2
        f = ImageFont.truetype(path, mid * SS)
        b = f.getbbox("0")
        if (b[3] - b[1]) >= cap * SS: hi = mid
        else: lo = mid + 1
    return lo

def render(path, ch, size, cap, stroke):
    f = ImageFont.truetype(path, size * SS)
    # digit cap box so every glyph shares the baseline
    zb = f.getbbox("0", stroke_width=stroke)
    top0, bot0 = zb[1], zb[3]
    H = cap * SS
    img = Image.new("L", (size * SS * 2, H + 8 * SS), 0)
    d = ImageDraw.Draw(img)
    d.text((SS * 2, -top0 + SS * 2 + (H - (bot0 - top0)) // 2), ch, font=f, fill=255, stroke_width=stroke, stroke_fill=255)
    bb = img.getbbox()
    if not bb: return None
    crop = img.crop((bb[0], SS * 2 - 0, bb[2], SS * 2 + H))   # keep full cell height
    small = crop.resize((max(1, round(crop.width / SS)), cap), Image.BOX)
    return small
