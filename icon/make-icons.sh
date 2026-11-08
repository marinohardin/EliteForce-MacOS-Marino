#!/bin/sh
# Build the Windows and Linux icons from the macOS app icon (macos/icon/AppIcon-1024.png)
# with sips (macOS) and a tiny Python ICO writer (PNG-compressed entries):
#   windows/icon.ico                 16, 24, 32, 48, 64, 128, 256 px
#   linux/eliteforce-marino.png      256 px (AppImage icon)
# Run it on a Mac when AppIcon-1024.png changes and commit the outputs; the packaging
# scripts only use the committed files.   icon/make-icons.sh
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="$ROOT/macos/icon/AppIcon-1024.png"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/efico.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
SIZES="16 24 32 48 64 128 256"
for s in $SIZES; do
	sips -z "$s" "$s" "$SRC" --out "$TMP/$s.png" >/dev/null
done
python3 -I - "$TMP" "$ROOT/windows/icon.ico" $SIZES <<'PY'
import struct, sys
tmp, out, sizes = sys.argv[1], sys.argv[2], [int(s) for s in sys.argv[3:]]
imgs = [open("%s/%d.png" % (tmp, s), "rb").read() for s in sizes]
data = struct.pack("<HHH", 0, 1, len(sizes))
off = 6 + 16 * len(sizes)
for s, png in zip(sizes, imgs):
    data += struct.pack("<BBBBHHII", s % 256, s % 256, 0, 0, 1, 32, len(png), off)  # 256 is stored as 0
    off += len(png)
open(out, "wb").write(data + b"".join(imgs))
PY
cp "$TMP/256.png" "$ROOT/linux/eliteforce-marino.png"
echo "wrote windows/icon.ico and linux/eliteforce-marino.png"
