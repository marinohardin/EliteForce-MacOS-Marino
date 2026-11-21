#!/usr/bin/env python3
"""Render the README banner (2560x800, shown at 1280x400) to docs/images/banner.png,
in the style of the social preview: the app icon and the project's name on a dark
starfield, "Marino's Build", and the platforms. No game artwork or logos.

    python3 macos/icon/make-readme-banner.py [--title-font tng-credits|jefferies]
        [--trek-fonts DIR] [--out PATH]

Needs Pillow, macOS for Avenir Next, and the Star Trek fonts that
make-social-preview.py uses (not in the repository).
"""
import argparse
import importlib.util
import os
import random

from PIL import Image, ImageDraw, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("social", os.path.join(HERE, "make-social-preview.py"))
sp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sp)

S = 2                      # drawn at 2x for Retina screens
W, H = 1280 * S, 400 * S
OUT = os.path.join(sp.ROOT, "docs", "images", "banner.png")
TITLE_FONTS = {"jefferies": sp.JEFFERIES, "tng-credits": sp.TNG_CREDITS}


def background():
    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        for x in range(W):
            t = min(1.0, ((x - 190 * S) ** 2 + (y - 200 * S) ** 2) ** 0.5 / (1000 * S))
            px[x, y] = (int(22 - 14 * t), int(34 - 22 * t), int(74 - 46 * t))
    haze = Image.new("L", (W, H), 0)
    hd = ImageDraw.Draw(haze)
    hd.ellipse((-260 * S, 220 * S, 380 * S, 640 * S), fill=70)
    hd.ellipse((960 * S, -260 * S, 1600 * S, 160 * S), fill=60)
    hd.ellipse((1050 * S, 250 * S, 1600 * S, 620 * S), fill=45)
    img.paste((46, 92, 190), (0, 0), haze.filter(ImageFilter.GaussianBlur(100 * S)))
    rnd = random.Random(1701)
    stars = Image.new("L", (W, H), 0)
    sd = ImageDraw.Draw(stars)
    for _ in range(200):
        x, y = rnd.randrange(W), rnd.randrange(H)
        r = rnd.choice((0.6, 0.8, 1.0, 1.0, 1.4, 1.8)) * S
        sd.ellipse((x - r, y - r, x + r, y + r), fill=rnd.randrange(90, 230))
    img.paste((255, 255, 255), (0, 0), stars.filter(ImageFilter.GaussianBlur(0.4 * S)))
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse((40 * S, 40 * S, 340 * S, 340 * S), fill=110)
    img.paste((214, 150, 60), (0, 0), glow.filter(ImageFilter.GaussianBlur(70 * S)))
    return img


def streak(img, x0, y0, x1, y1, alpha):
    sp.W, sp.H = W, H
    sp.streak(img, x0, y0, x1, y1, alpha)


def pill(d, x, y, parts):
    """A rounded tag holding (text, font, fill) parts; returns the next x."""
    h = 40 * S
    w = sum(d.textlength(t, font=f) for t, f, _ in parts) + 30 * S
    d.rounded_rectangle((x, y, x + w, y + h), radius=h // 2, fill=(255, 255, 255, 22),
        outline=(255, 255, 255, 60), width=2 * S)
    sp.runs(d, x + 15 * S, y + h / 2 + 7 * S, parts)
    return x + w + 12 * S


def ink_left(d, text, f):
    return d.textbbox((0, 0), text, font=f, anchor="ls")[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--title-font", choices=sorted(TITLE_FONTS), default="tng-credits")
    ap.add_argument("--trek-fonts", default=sp.TREK_FONTS)
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()
    sp.find_trek_fonts(args.trek_fonts)
    title_face = TITLE_FONTS[args.title_font]

    img = background()
    streak(img, -10 * S, 70 * S, 130 * S, 30 * S, 150)
    streak(img, 1120 * S, 386 * S, 1290 * S, 360 * S, 150)
    img = img.convert("RGBA")

    size = 260 * S
    icon = Image.open(sp.ICON).convert("RGBA").resize((size, size), Image.LANCZOS)
    pos = (60 * S, 70 * S)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 160), (pos[0], pos[1] + 10 * S), icon.getchannel("A"))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(16 * S)))
    img.alpha_composite(icon, pos)

    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    x, right = 370 * S, W - 50 * S
    f = sp.font(sp.JEFFERIES, 24 * S)
    d.text((x - ink_left(d, "Star Trek", f), 80 * S), "Star Trek: Voyager: Elite Force",
        font=f, fill=sp.SOFT, anchor="ls")

    size = 96 * S
    while True:
        ft = sp.font(title_face, size)
        l, _, r, _ = d.textbbox((0, 0), "AwayTeamEF", font=ft, anchor="ls")
        if r - l <= right - x or size <= 40 * S:
            break
        size -= 2 * S
    sp.runs(d, x - l, 172 * S, (("AwayTeam", ft, sp.WHITE), ("EF", ft, sp.GOLD)))

    ftng = sp.font(sp.TNG, 50 * S)
    d.text((x - ink_left(d, "Away", ftng), 232 * S), "Away Team Edition", font=ftng,
        fill=sp.GOLD, anchor="ls")
    # "Marino's Build" on the same baseline, after a thin divider
    ex = x - ink_left(d, "Away", ftng) + d.textlength("Away Team Edition", font=ftng) + 22 * S
    d.rectangle((ex, 200 * S, ex + 2 * S, 232 * S), fill=sp.GOLD + (150,))
    fm = sp.font(sp.DEMI, 30 * S)
    d.text((ex + 22 * S, 231 * S), "Marino’s Build", font=fm, fill=sp.WHITE, anchor="ls")

    # the tags: what you can play, then where
    fp, fpb = sp.font(sp.MEDIUM, 21 * S), sp.font(sp.BOLD, 21 * S)
    for y, tags in ((262, ((("Single player", fpb, sp.WHITE),),
                           (("Two-player co-op campaign", fpb, sp.GOLD),),
                           (("Holomatch multiplayer", fpb, sp.WHITE),))),
                    (314, ((("macOS", fpb, sp.WHITE),), (("Windows", fp, sp.SOFT),),
                           (("Linux & Steam Deck", fp, sp.SOFT),)))):
        px = x
        for parts in tags:
            px = pill(d, px, y * S, parts)
    img.alpha_composite(over)

    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    img.convert("RGB").save(args.out, optimize=True)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
