#!/usr/bin/env python3
"""Render the GitHub social preview image (1280x640) to docs/social-preview.png:
the app icon and the project's name on a dark starfield. No game artwork or
trademarked logos.

    python3 macos/icon/make-social-preview.py      (needs Pillow; macOS for the Avenir Next font)

Upload it under the repository's Settings > General > Social preview.
"""
import os
import random

from PIL import Image, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ICON = os.path.join(HERE, "AppIcon-1024.png")
OUT = os.path.join(ROOT, "docs", "social-preview.png")
W, H = 1280, 640
FONT = "/System/Library/Fonts/Avenir Next.ttc"
BOLD, DEMI, MEDIUM, REGULAR, HEAVY = 0, 2, 5, 7, 8

GOLD = (236, 178, 58)
WHITE = (244, 246, 252)
SOFT = (170, 184, 214)
DIM = (118, 132, 168)


def font(face, size):
    return ImageFont.truetype(FONT, size, index=face)


def background():
    img = Image.new("RGB", (W, H))
    px = img.load()
    # deep navy, lighter towards the icon on the left
    for y in range(H):
        for x in range(W):
            d = ((x - 300) ** 2 + (y - 320) ** 2) ** 0.5 / 900
            t = min(1.0, d)
            px[x, y] = (int(22 - 14 * t), int(34 - 22 * t), int(74 - 46 * t))
    # stars
    rnd = random.Random(1701)
    stars = Image.new("L", (W, H), 0)
    sd = ImageDraw.Draw(stars)
    for _ in range(260):
        x, y = rnd.randrange(W), rnd.randrange(H)
        r = rnd.choice((0.6, 0.8, 1.0, 1.0, 1.4, 1.8))
        sd.ellipse((x - r, y - r, x + r, y + r), fill=rnd.randrange(90, 230))
    stars = stars.filter(ImageFilter.GaussianBlur(0.4))
    img.paste((255, 255, 255), (0, 0), stars)
    # warm glow behind the icon
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse((90, 110, 510, 530), fill=120)
    glow = glow.filter(ImageFilter.GaussianBlur(90))
    img.paste((214, 150, 60), (0, 0), glow)
    return img


def pill(draw, x, y, text, f):
    l, t, r, b = draw.textbbox((0, 0), text, font=f)
    w, h = r - l + 36, 46
    draw.rounded_rectangle((x, y, x + w, y + h), radius=h // 2, fill=(255, 255, 255, 22),
        outline=(255, 255, 255, 60), width=2)
    draw.text((x + 18, y + h / 2), text, font=f, fill=SOFT, anchor="lm")
    return x + w + 14


def main():
    img = background().convert("RGBA")
    icon = Image.open(ICON).convert("RGBA").resize((430, 430), Image.LANCZOS)
    img.alpha_composite(icon, (85, 105))

    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    x = 560
    d.text((x, 150), "Elite Force", font=font(HEAVY, 104), fill=WHITE)
    d.text((x + 4, 268), "for Mac", font=font(DEMI, 54), fill=GOLD)
    d.rectangle((x + 4, 348, x + 120, 352), fill=GOLD)
    d.text((x + 4, 378), "Single player + Holomatch multiplayer", font=font(MEDIUM, 34), fill=SOFT)
    px = x + 4
    for t in ("Apple Silicon", "LAN & internet servers", "Retina"):
        px = pill(d, px, 436, t, font(MEDIUM, 24))
    d.text((x + 4, 540), "Marino's Build  ·  unofficial fan port",
        font=font(REGULAR, 22), fill=DIM)
    img.alpha_composite(over)
    img.convert("RGB").save(OUT, optimize=True)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
