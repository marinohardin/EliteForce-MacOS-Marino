#!/usr/bin/env python3
"""Turn source.webp (rounded-square artwork on a white background with a
baked-in grey drop shadow, no alpha) into AppIcon-1024.png: a 1024x1024 macOS
app icon with a transparent outside, laid out on Apple's icon grid (824 pt
body centred in 1024) with a soft shadow of its own.

Only near-white / light-grey pixels connected to the image border are
removed (flood fill), so light parts inside the dark rim (highlights, the
"SP" letters) are never touched.

    python3 macos/icon/mask-icon.py      (needs Pillow; run once, commit the png)
"""
import os
from collections import deque

from PIL import Image, ImageChops, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "source.webp")
DST = os.path.join(HERE, "AppIcon-1024.png")
CANVAS, BODY = 1024, 824          # Apple macOS icon grid


def background_mask(im):
    """255 where a pixel is light, neutral and connected to the border."""
    w, h = im.size
    px = im.load()
    bg = Image.new("L", im.size, 0)
    out = bg.load()

    def light(p):
        return min(p) >= 150 and max(p) - min(p) <= 30

    q = deque()
    for x in range(w):
        q.extend(((x, 0), (x, h - 1)))
    for y in range(h):
        q.extend(((0, y), (w - 1, y)))
    while q:
        x, y = q.popleft()
        if out[x, y] or not light(px[x, y]):
            continue
        out[x, y] = 255
        if x > 0: q.append((x - 1, y))
        if x < w - 1: q.append((x + 1, y))
        if y > 0: q.append((x, y - 1))
        if y < h - 1: q.append((x, y + 1))
    return bg


def main():
    im = Image.open(SRC).convert("RGB")
    bg = background_mask(im)
    body = ImageChops.invert(bg)
    # drop the 1-2 px anti-aliasing band (rim blended with white/shadow),
    # then soften the edge again for anti-aliasing
    alpha = body.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.GaussianBlur(1.0))
    # partially transparent edge pixels take the colour of the dark rim
    # just inside, so no light fringe can show on dark backgrounds
    rim = im.filter(ImageFilter.MinFilter(7))
    edge = alpha.point(lambda a: 255 if 0 < a < 255 else 0)
    rgb = Image.composite(rim, im, edge)
    art = rgb.convert("RGBA")
    art.putalpha(alpha)
    art = art.crop(alpha.getbbox())

    # fit the body into the 824 pt grid square
    s = BODY / max(art.size)
    art = art.resize((round(art.width * s), round(art.height * s)), Image.LANCZOS)
    x, y = (CANVAS - art.width) // 2, (CANVAS - art.height) // 2

    shadow_alpha = Image.new("L", (CANVAS, CANVAS), 0)
    shadow_alpha.paste(art.getchannel("A"), (x, y + 10))
    shadow_alpha = shadow_alpha.filter(ImageFilter.GaussianBlur(14)).point(lambda a: a * 45 // 100)
    out = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    out.putalpha(shadow_alpha)
    out.alpha_composite(art, (x, y))
    out.save(DST, optimize=True)
    print("wrote %s (body %dx%d at %d,%d)" % (DST, art.width, art.height, x, y))


if __name__ == "__main__":
    main()
