#!/bin/sh
# Build (inside the Steam Runtime sniper SDK container, see linux/docker-build.sh) the minimal, LGPL-2.1-or-later FFmpeg the Linux
# engine uses for Bink cinematics (sp/bink_ff.c): the Bink demuxer, the Bink
# video/audio decoders and swscale, nothing else. Same configuration as
# macos/build-ffmpeg.sh; no --enable-gpl / --enable-version3 and
# --disable-autodetect, so it links only against system libraries.
#
#   linux/build-ffmpeg.sh        ->  build-ffmpeg/linux/install/{include,lib}
#
# Version: deps/versions.sh. Record the version in THIRD_PARTY_LICENSES.md.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/deps/versions.sh"

OUT="$ROOT/build-ffmpeg"
TARBALL="$OUT/ffmpeg-$FFMPEG_VERSION.tar.xz"
WORK="$OUT/linux"
SRC="$WORK/ffmpeg-$FFMPEG_VERSION"
PREFIX="$WORK/install"
JOBS="$(nproc)"

mkdir -p "$OUT" "$WORK"
if [ ! -f "$TARBALL" ]; then
	echo "Downloading FFmpeg $FFMPEG_VERSION"
	curl -fL --retry 8 --retry-delay 10 --retry-all-errors -o "$TARBALL.part" "https://ffmpeg.org/releases/ffmpeg-$FFMPEG_VERSION.tar.xz"
	mv "$TARBALL.part" "$TARBALL"
fi
ef_sha256_check "$TARBALL" "$FFMPEG_SHA256" ||
	{ echo "checksum mismatch for $TARBALL" >&2; exit 1; }

rm -rf "$SRC" "$PREFIX"
tar -xJf "$TARBALL" -C "$WORK"

cd "$SRC"
# --disable-asm: the bink decoders need none (and no nasm); -fPIC for shared libs.
./configure \
	--prefix="$PREFIX" \
	--arch=x86_64 --enable-pic \
	--disable-asm \
	--enable-shared --disable-static \
	--disable-programs --disable-doc --disable-network \
	--disable-autodetect --disable-everything \
	--disable-avdevice --disable-avfilter --disable-swresample \
	--enable-avformat --enable-avcodec --enable-swscale \
	--enable-demuxer=bink \
	--enable-decoder=bink,binkaudio_rdft,binkaudio_dct
make -j"$JOBS"
make install
# the LGPL text from this exact source, for the release packages (the source tree is removed below)
mkdir -p "$PREFIX/licenses"
cp "$SRC/COPYING.LGPLv2.1" "$PREFIX/licenses/FFmpeg-COPYING.LGPLv2.1.txt"

# refuse a GPL build (configure prints the license; config.h records it)
grep -q 'FFMPEG_LICENSE "LGPL version 2.1 or later"' "$SRC/config.h" ||
	{ echo "FFmpeg was not configured as LGPL-2.1-or-later" >&2; exit 1; }

# list the shared-library dependencies (system libraries only expected)
for so in "$PREFIX"/lib/lib*.so; do
	echo "$(basename "$so"): $(readelf -d "$so" | awk '/NEEDED/{printf "%s ", $5}')"
done
rm -rf "$SRC"
echo "Built LGPL FFmpeg $FFMPEG_VERSION for Linux x86_64 in $PREFIX"
