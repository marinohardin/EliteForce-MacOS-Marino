#!/usr/bin/env python3
"""Render the GitHub social preview image (1280x640) to docs/social-preview.png:
the app icon, the project's name in plain text on a dark starfield, and the
repository with its owner's avatar along the bottom. No game artwork or logos.

    python3 macos/icon/make-social-preview.py [--avatar costume|github]
        [--trek-fonts DIR] [--out PATH]

Needs Pillow; macOS for the Avenir Next font; and, in --trek-fonts (searched
recursively, default ~/Downloads/StarTrekFonts), Jefferies.ttf, TNG_Credits.ttf
and TNG_Title.ttf. Those fonts are not in the repository. Upload the result under Settings > General > Social preview.
"""
import argparse
import glob
import os
import random

from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ICON = os.path.join(HERE, "AppIcon-1024.png")
AVATARS = {
    "costume": os.path.join(HERE, "avatar-costume.png"),
    "github": os.path.join(HERE, "avatar-github.png"),
}
OUT = os.path.join(ROOT, "docs", "social-preview.png")
OWNER, REPO = "marinohardin", "AwayTeamEF"
W, H = 1280, 640
FONT = "/System/Library/Fonts/Avenir Next.ttc"
BOLD, DEMI, MEDIUM, REGULAR, HEAVY = 0, 2, 5, 7, 8

GOLD = (236, 178, 58)
WHITE = (244, 246, 252)
SOFT = (170, 184, 214)
DIM = (118, 132, 168)


TREK_FONTS = os.path.expanduser("~/Downloads/StarTrekFonts")
JEFFERIES, TNG, TNG_CREDITS = "Jefferies.ttf", "TNG_Title.ttf", "TNG_Credits.ttf"
trek = {}  # font file name -> path, filled in main()


def font(face, size):
    """An Avenir Next face by index, or a Star Trek font by file name."""
    if isinstance(face, str):
        return ImageFont.truetype(trek[face], size)
    return ImageFont.truetype(FONT, size, index=face)


def find_trek_fonts(folder):
    for name in (JEFFERIES, TNG, TNG_CREDITS):
        hits = glob.glob(os.path.join(folder, "**", name), recursive=True)
        if not hits:
            raise SystemExit(f"{name} not found under {folder}; pass --trek-fonts DIR")
        trek[name] = hits[0]


def streak(img, x0, y0, x1, y1, alpha):
    """A thin gold light streak that fades out towards both ends."""
    line = Image.new("L", (W, H), 0)
    d = ImageDraw.Draw(line)
    n = 40
    for i in range(n):
        t0, t1 = i / n, (i + 1) / n
        a = int(alpha * (1 - abs(2 * (t0 + t1) / 2 - 1)))
        d.line((x0 + (x1 - x0) * t0, y0 + (y1 - y0) * t0,
                x0 + (x1 - x0) * t1, y0 + (y1 - y0) * t1), fill=a, width=2)
    img.paste(GOLD, (0, 0), line.filter(ImageFilter.GaussianBlur(0.6)))


def background():
    img = Image.new("RGB", (W, H))
    px = img.load()
    # deep navy, lighter towards the icon on the left
    for y in range(H):
        for x in range(W):
            d = ((x - 300) ** 2 + (y - 300) ** 2) ** 0.5 / 900
            t = min(1.0, d)
            px[x, y] = (int(22 - 14 * t), int(34 - 22 * t), int(74 - 46 * t))
    # blue nebula haze in the corners
    haze = Image.new("L", (W, H), 0)
    hd = ImageDraw.Draw(haze)
    hd.ellipse((-260, 380, 420, 900), fill=70)
    hd.ellipse((900, -300, 1600, 260), fill=60)
    hd.ellipse((1000, 460, 1600, 900), fill=50)
    img.paste((46, 92, 190), (0, 0), haze.filter(ImageFilter.GaussianBlur(110)))
    # stars
    rnd = random.Random(1701)
    stars = Image.new("L", (W, H), 0)
    sd = ImageDraw.Draw(stars)
    for _ in range(260):
        x, y = rnd.randrange(W), rnd.randrange(H)
        r = rnd.choice((0.6, 0.8, 1.0, 1.0, 1.4, 1.8))
        sd.ellipse((x - r, y - r, x + r, y + r), fill=rnd.randrange(90, 230))
    img.paste((255, 255, 255), (0, 0), stars.filter(ImageFilter.GaussianBlur(0.4)))
    # warm glow behind the icon
    glow = Image.new("L", (W, H), 0)
    ImageDraw.Draw(glow).ellipse((110, 90, 490, 470), fill=110)
    img.paste((214, 150, 60), (0, 0), glow.filter(ImageFilter.GaussianBlur(90)))
    streak(img, -10, 112, 120, 62, 150)
    streak(img, 1150, 616, 1290, 588, 150)
    return img


def runs(draw, x, y, parts):
    """Draw (text, font, fill) parts one after another on baseline y; return the end x."""
    for text, f, fill in parts:
        draw.text((x, y), text, font=f, fill=fill, anchor="ls")
        x += draw.textlength(text, font=f)
    return x


def pill(draw, x, y, parts):
    """A rounded tag holding (text, font, fill) parts."""
    w, h = sum(draw.textlength(t, font=f) for t, f, _ in parts) + 36, 46
    draw.rounded_rectangle((x, y, x + w, y + h), radius=h // 2, fill=(255, 255, 255, 22),
        outline=(255, 255, 255, 60), width=2)
    runs(draw, x + 18, y + h / 2 + 8, parts)
    return x + w + 14


def avatar(path, size):
    """The avatar in a circle with a thin light ring, supersampled for a clean edge."""
    s = size * 4
    pic = Image.open(path).convert("RGBA").resize((s, s), Image.LANCZOS)
    mask = Image.new("L", (s, s), 0)
    ImageDraw.Draw(mask).ellipse((0, 0, s - 1, s - 1), fill=255)
    pic.putalpha(ImageChops.multiply(pic.getchannel("A"), mask))
    ring = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    ImageDraw.Draw(ring).ellipse((6, 6, s - 7, s - 7), outline=(255, 255, 255, 110), width=12)
    pic.alpha_composite(ring)
    return pic.resize((size, size), Image.LANCZOS)


def fit(draw, text, face, size, width):
    """The largest font at or below size that fits text in width."""
    while size > 40 and draw.textlength(text, font=font(face, size)) > width:
        size -= 2
    return font(face, size)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--avatar", choices=sorted(AVATARS), default="costume")
    ap.add_argument("--trek-fonts", default=TREK_FONTS)
    ap.add_argument("--out", default=OUT)
    args = ap.parse_args()
    find_trek_fonts(args.trek_fonts)

    img = background().convert("RGBA")
    icon = Image.open(ICON).convert("RGBA").resize((400, 400), Image.LANCZOS)
    pos = (100, 72)
    shadow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    shadow.paste((0, 0, 0, 160), (pos[0], pos[1] + 12), icon.getchannel("A"))
    img.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(18)))
    img.alpha_composite(icon, pos)

    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    x = 565
    right = W - 40
    d.text((x + 2, 90), "Star Trek: Voyager: Elite Force",
        font=fit(d, "Star Trek: Voyager: Elite Force", JEFFERIES, 26, right - x), fill=SOFT, anchor="ls")
    # TNG Credits is italic and overhangs its origin on the left, so measure its ink
    size = 104
    while True:
        f_title = font(TNG_CREDITS, size)
        l, _, r, _ = d.textbbox((0, 0), "AwayTeamEF", font=f_title, anchor="ls")
        if r - l <= right - x or size <= 60:
            break
        size -= 2
    runs(d, x - l, 188, (("AwayTeam", f_title, WHITE), ("EF", f_title, GOLD)))
    d.text((x + 2, 260), "Away Team Edition", font=font(TNG, 54), fill=GOLD, anchor="ls")
    d.rectangle((x + 2, 288, x + 116, 291), fill=GOLD)
    f_tag = fit(d, "Single player, co-op and Holomatch multiplayer", MEDIUM, 30, right - x)
    f_tag_b = font(BOLD, f_tag.size)
    runs(d, x + 2, 336, (("Single player, ", f_tag, SOFT), ("co-op", f_tag_b, GOLD),
                         (" and Holomatch multiplayer", f_tag, SOFT)))
    f_pill, f_pill_b = font(MEDIUM, 23), font(BOLD, 23)
    for row, tags in ((368, ("MacOS (Apple Silicon)", "Windows", "Linux")),
                      (426, ("Unofficial fan project", "Co-op single player campaign"))):
        px = x + 2
        for t in tags:
            if t.startswith("Co-op"):
                parts = (("Co-op", f_pill_b, GOLD), (t[5:], f_pill, SOFT))
            else:
                parts = ((t, f_pill, SOFT),)
            px = pill(d, px, row, parts)
    img.alpha_composite(over)

    # the repository along the bottom: avatar, owner/REPO, a fading gold rule
    av = 84
    ax, ay = 130, 505
    img.alpha_composite(avatar(AVATARS[args.avatar], av), (ax, ay))
    over = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    f_owner, f_repo = font(MEDIUM, 42), font(BOLD, 42)
    tx, ty = ax + av + 26, ay + av / 2
    owner = OWNER + "/"
    d.text((tx, ty), owner, font=f_owner, fill=SOFT, anchor="lm")
    rx = tx + d.textlength(owner, font=f_owner)
    d.text((rx, ty), REPO, font=f_repo, fill=WHITE, anchor="lm")
    lx0 = int(rx + d.textlength(REPO, font=f_repo) + 26)
    lx1 = min(W - 120, lx0 + 300)
    for i in range(lx0, lx1):
        a = int(220 * (1 - (i - lx0) / (lx1 - lx0)) ** 1.4)
        d.line((i, ty, i, ty + 2), fill=GOLD + (a,))
    img.alpha_composite(over)

    img.convert("RGB").save(args.out, optimize=True)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
