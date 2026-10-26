#!/usr/bin/env python3
"""Render the DMG window background: "Elite Force for Mac" at the top and an
arrow from where the app icon sits to the Applications shortcut, with
"Drag to Applications to install". Writes background.png (660x450 points),
background@2x.png and the combined background.tiff that make-release.sh uses.

    python3 macos/release/make-dmg-background.py      (needs Pillow; macOS for
                                                     the font and tiffutil)

Keep the icon positions in make-release.sh (APP_X, APPS_X, ICON_Y, and the Read
Me at 330,366 below the text) in step with APP_X / APPS_X / ICON_Y here. Light
background on purpose: Finder draws the icon labels in dark text.
"""
import os
import subprocess

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
FONT = "/System/Library/Fonts/Avenir Next.ttc"
BOLD, DEMI, MEDIUM, REGULAR = 0, 2, 5, 7

W, H = 660, 450                 # the window's content size, in points
APP_X, APPS_X, ICON_Y = 165, 495, 190   # icon centres, in points (as make-release.sh)

NAVY = (24, 36, 78)
GOLD = (214, 150, 40)
SOFT = (92, 104, 132)


def font(face, size):
    return ImageFont.truetype(FONT, size, index=face)


def render(scale):
    w, h = W * scale, H * scale
    img = Image.new("RGB", (w, h))
    px = img.load()
    # soft vertical gradient, near-white to a pale blue-grey
    for y in range(h):
        t = y / (h - 1)
        c = (int(250 - 18 * t), int(251 - 14 * t), int(255 - 8 * t))
        for x in range(w):
            px[x, y] = c
    d = ImageDraw.Draw(img)
    s = scale

    # title
    d.text((w / 2, 40 * s), "Elite Force for Mac", font=font(BOLD, 30 * s), fill=NAVY, anchor="mm")
    d.text((w / 2, 72 * s), "Marino's Build", font=font(DEMI, 15 * s), fill=GOLD, anchor="mm")

    # arrow between the two icons
    y = ICON_Y * s
    x0, x1 = (APP_X + 92) * s, (APPS_X - 92) * s
    d.line((x0, y, x1 - 14 * s, y), fill=GOLD, width=6 * s)
    d.polygon([(x1, y), (x1 - 22 * s, y - 14 * s), (x1 - 22 * s, y + 14 * s)], fill=GOLD)
    d.text((w / 2, y - 28 * s), "Drag to Applications", font=font(DEMI, 15 * s), fill=NAVY, anchor="mm")
    d.text((w / 2, y + 28 * s), "to install", font=font(MEDIUM, 13 * s), fill=SOFT, anchor="mm")

    # next step
    d.text((w / 2, 292 * s), "Then open Elite Force and choose your Elite Force folder.",
        font=font(REGULAR, 13 * s), fill=SOFT, anchor="mm")
    return img


def main():
    one = os.path.join(HERE, "background.png")
    two = os.path.join(HERE, "background@2x.png")
    tiff = os.path.join(HERE, "background.tiff")
    render(1).save(one, dpi=(72, 72))
    render(2).save(two, dpi=(144, 144))
    # one TIFF holding both resolutions: Finder picks the Retina one when it can
    subprocess.run(["tiffutil", "-cathidpicheck", one, two, "-out", tiff], check=True,
        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("wrote", tiff)


if __name__ == "__main__":
    main()
