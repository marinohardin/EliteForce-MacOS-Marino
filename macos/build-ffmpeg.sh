#!/bin/sh
# Build the minimal, LGPL-2.1-or-later FFmpeg the macOS engine uses for Bink
# cinematics (sp/bink_ff.c): the Bink demuxer, the Bink video/audio decoders
# and swscale, nothing else. No --enable-gpl / --enable-version3 and
# --disable-autodetect, so it links only against system libraries.
#
#   macos/build-ffmpeg.sh        ->  build-ffmpeg/install/{include,lib}
#
# The engine CMake picks build-ffmpeg/install up automatically (EF_FFMPEG_PREFIX)
# and make-app.sh bundles its dylibs. Bump FFMPEG_VERSION + FFMPEG_SHA256 to
# update; record the version in THIRD_PARTY_LICENSES.md.
set -eu

FFMPEG_VERSION="9.0.2"
FFMPEG_SHA256="8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-ffmpeg"
TARBALL="$OUT/ffmpeg-$FFMPEG_VERSION.tar.xz"
SRC="$OUT/ffmpeg-$FFMPEG_VERSION"
PREFIX="$OUT/install"

mkdir -p "$OUT"
if [ ! -f "$TARBALL" ]; then
	echo "Downloading FFmpeg $FFMPEG_VERSION"
	curl -fL -o "$TARBALL.part" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
	mv "$TARBALL.part" "$TARBALL"
fi
echo "$FFMPEG_SHA256  $TARBALL" | shasum -a 256 -c - >/dev/null ||
	{ echo "checksum mismatch for $TARBALL" >&2; exit 1; }

rm -rf "$SRC" "$PREFIX"
tar -xJf "$TARBALL" -C "$OUT"

cd "$SRC"
./configure \
	--prefix="$PREFIX" \
	--install-name-dir='@rpath' \
	--arch=arm64 --cc=clang \
	--enable-shared --disable-static \
	--disable-programs --disable-doc --disable-network \
	--disable-autodetect --disable-everything \
	--disable-avdevice --disable-avfilter --disable-swresample \
	--enable-avformat --enable-avcodec --enable-swscale \
	--enable-demuxer=bink \
	--enable-decoder=bink,binkaudio_rdft,binkaudio_dct
make -j"$(sysctl -n hw.ncpu)"
make install

# refuse a GPL build (configure prints the license; config.h records it)
grep -q 'FFMPEG_LICENSE "LGPL version 2.1 or later"' "$SRC/config.h" ||
	{ echo "FFmpeg was not configured as LGPL-2.1-or-later" >&2; exit 1; }
echo "Built LGPL FFmpeg $FFMPEG_VERSION in $PREFIX"
