#!/usr/bin/env python3
"""Regenerate cmake/windows/misc/lilium.ico (the efsp.exe icon win_resource.rc embeds under
mingw as "misc/lilium.ico") from macos/icon/AppIcon-1024.png. Needs Pillow.

    python3 cmake/windows/make-ico.py
"""
import os
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.dirname(os.path.dirname(here))
src = os.path.join(root, "macos", "icon", "AppIcon-1024.png")
out = os.path.join(here, "misc", "lilium.ico")
os.makedirs(os.path.dirname(out), exist_ok=True)
img = Image.open(src).convert("RGBA")
img.save(out, format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print(out, os.path.getsize(out), "bytes")
