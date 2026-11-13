#!/bin/sh
# Build the libraries the Linux x86_64 build bundles other than FFmpeg (see
# build-ffmpeg.sh): SDL3 (X11, Wayland, PipeWire, PulseAudio, ALSA backends, all
# dlopen'ed at run time as SDL does by default), sdl2-compat (the SDL2 API the
# engine uses, on SDL3), and a static liblzma. The system libvulkan.so.1 is
# loaded by SDL at run time.
#
#   linux/build-deps.sh        ->  build-deps/linux/install/{include,lib}
#
# Run it inside the Steam Runtime 3 "sniper" SDK container (linux/docker-build.sh
# does), so the result runs on any glibc/libstdc++ the Steam Runtime supports.
# Needs cmake, ninja and the SDL3 development packages (the sniper SDK has them).
# Versions: deps/versions.sh.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/deps/versions.sh"

OUT="$ROOT/build-deps"				# download cache
WORK="$OUT/linux"
PREFIX="$WORK/install"
JOBS="$(nproc)"

mkdir -p "$OUT" "$WORK"
# fetch NAME URL SHA256: download (once) and verify; prints the tarball path
fetch() {
	tarball="$OUT/$1"
	if [ ! -f "$tarball" ]; then
		echo "Downloading $1" >&2
		curl -fL --retry 8 --retry-delay 10 --retry-all-errors -o "$tarball.part" "$2"
		mv "$tarball.part" "$tarball"
	fi
	ef_sha256_check "$tarball" "$3" ||
		{ echo "checksum mismatch for $tarball" >&2; exit 1; }
	echo "$tarball"
}

# cmake_build SRCDIR [cmake options...]: configure, build and install into PREFIX,
# then drop the source/build tree (disk space)
cmake_build() {
	src="$1"; shift
	cmake -S "$src" -B "$src/_build" -G Ninja \
		-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_LIBDIR=lib \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DCMAKE_PREFIX_PATH="$PREFIX" \
		"$@"
	cmake --build "$src/_build" -j"$JOBS"
	cmake --install "$src/_build"
	rm -rf "$src"
}

rm -rf "$PREFIX"

t="$(fetch "xz-$XZ_VERSION.tar.xz" \
	"https://github.com/tukaani-project/xz/releases/download/v$XZ_VERSION/xz-$XZ_VERSION.tar.xz" "$XZ_SHA256")"
rm -rf "$WORK/xz-$XZ_VERSION" && tar -xJf "$t" -C "$WORK"
# liblzma's license (0BSD) and the package's licensing summary, for the release packages
mkdir -p "$PREFIX/licenses"
cp "$WORK/xz-$XZ_VERSION/COPYING" "$PREFIX/licenses/xz-COPYING.txt"
cp "$WORK/xz-$XZ_VERSION/COPYING.0BSD" "$PREFIX/licenses/xz-COPYING.0BSD.txt"
cmake_build "$WORK/xz-$XZ_VERSION" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
	-DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF \
	-DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SYMLINKS=OFF -DXZ_TOOL_SCRIPTS=OFF

t="$(fetch "SDL3-$SDL3_VERSION.tar.gz" \
	"https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" "$SDL3_SHA256")"
rm -rf "$WORK/SDL3-$SDL3_VERSION" && tar -xzf "$t" -C "$WORK"
cmake_build "$WORK/SDL3-$SDL3_VERSION" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF \
	-DSDL_X11=ON -DSDL_WAYLAND=ON -DSDL_PIPEWIRE=ON -DSDL_PULSEAUDIO=ON -DSDL_ALSA=ON \
	-DSDL_X11_SHARED=ON -DSDL_WAYLAND_SHARED=ON -DSDL_PIPEWIRE_SHARED=ON -DSDL_PULSEAUDIO_SHARED=ON -DSDL_ALSA_SHARED=ON \
	-DSDL_RPATH=OFF

t="$(fetch "sdl2-compat-$SDL2COMPAT_VERSION.tar.gz" \
	"https://github.com/libsdl-org/sdl2-compat/releases/download/release-$SDL2COMPAT_VERSION/sdl2-compat-$SDL2COMPAT_VERSION.tar.gz" "$SDL2COMPAT_SHA256")"
rm -rf "$WORK/sdl2-compat-$SDL2COMPAT_VERSION" && tar -xzf "$t" -C "$WORK"
cmake_build "$WORK/sdl2-compat-$SDL2COMPAT_VERSION" -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF
# installed as sdl2-compat.pc; "pkg-config sdl2" wants sdl2.pc, as on macOS
ln -sf sdl2-compat.pc "$PREFIX/lib/pkgconfig/sdl2.pc"

echo "Built SDL3 $SDL3_VERSION, sdl2-compat $SDL2COMPAT_VERSION, liblzma $XZ_VERSION for Linux x86_64 in $PREFIX"
