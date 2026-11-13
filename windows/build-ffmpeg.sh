#!/bin/sh
# Cross-build (GCC mingw-w64) the minimal, LGPL-2.1-or-later FFmpeg the Windows
# engine uses for Bink cinematics (sp/bink_ff.c): the Bink demuxer, the Bink
# video/audio decoders and swscale, nothing else. Same configuration as
# macos/build-ffmpeg.sh; no --enable-gpl / --enable-version3 and
# --disable-autodetect, so it links only against system DLLs.
#
#   windows/build-ffmpeg.sh        ->  build-ffmpeg/windows/install/{bin,include,lib}
#
# Version: deps/versions.sh. Record the version in THIRD_PARTY_LICENSES.md.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/deps/versions.sh"

OUT="$ROOT/build-ffmpeg"
TARBALL="$OUT/ffmpeg-$FFMPEG_VERSION.tar.xz"
WORK="$OUT/windows"
SRC="$WORK/ffmpeg-$FFMPEG_VERSION"
PREFIX="$WORK/install"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
TRIPLE=x86_64-w64-mingw32

command -v $TRIPLE-gcc >/dev/null ||
	{ echo "$TRIPLE-gcc not found (brew install mingw-w64)" >&2; exit 1; }

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
# -static-libgcc, static libgcc and winpthread (clock_gettime/nanosleep): no libgcc_s_seh-1.dll / libwinpthread-1.dll dependency. The bink decoders use no
# asm (--disable-asm also avoids needing nasm for this tiny build).
./configure \
	--prefix="$PREFIX" \
	--target-os=mingw32 --arch=x86_64 --cross-prefix=$TRIPLE- --enable-cross-compile \
	--extra-ldflags="-static-libgcc" --extra-libs="-Wl,-Bstatic -lwinpthread -Wl,-Bdynamic" \
	--disable-asm --disable-pthreads --enable-w32threads \
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

# the DLLs must depend on system DLLs only
for dll in "$PREFIX"/bin/*.dll; do
	echo "$(basename "$dll"): $($TRIPLE-objdump -p "$dll" | awk '/DLL Name/{printf "%s ", $3}')"
	if $TRIPLE-objdump -p "$dll" | grep 'DLL Name' | grep -Eiq 'libgcc|libwinpthread|libstdc'; then
		echo "$dll depends on a mingw runtime DLL" >&2; exit 1
	fi
done
rm -rf "$SRC"
echo "Built LGPL FFmpeg $FFMPEG_VERSION for Windows x86_64 in $PREFIX"
