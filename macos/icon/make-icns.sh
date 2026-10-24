#!/bin/sh
# Build an .icns from the 1024x1024 app icon PNG with sips + iconutil.
#   macos/icon/make-icns.sh [OUT.icns]     (default: macos/icon/AppIcon.icns)
# AppIcon-1024.png is made from source.webp by mask-icon.py (run once).
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/AppIcon-1024.png"
OUT="${1:-$HERE/AppIcon.icns}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/appicon.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT
SET="$TMP/AppIcon.iconset"
mkdir "$SET"
for s in 16 32 128 256 512; do
	sips -z "$s" "$s" "$SRC" --out "$SET/icon_${s}x${s}.png" >/dev/null
	d=$((s * 2))
	sips -z "$d" "$d" "$SRC" --out "$SET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$SET" -o "$OUT"
