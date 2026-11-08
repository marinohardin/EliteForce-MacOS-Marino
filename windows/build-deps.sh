#!/bin/sh
# Cross-build (from macOS or Linux, GCC mingw-w64) the libraries the Windows
# x86_64 build bundles other than FFmpeg (see build-ffmpeg.sh):
#   SDL3 and sdl2-compat (the SDL2 API the engine uses, on SDL3), both shared
#   (SDL3.dll, SDL2.dll + import libraries), and a static liblzma.
# There is no Vulkan loader: the GPU driver ships vulkan-1.dll.
#
#   windows/build-deps.sh        ->  build-deps/windows/install/{bin,include,lib}
#
# Needs x86_64-w64-mingw32-gcc (Homebrew: mingw-w64; Ubuntu: gcc-mingw-w64-x86-64
# + g++-mingw-w64-x86-64), cmake and ninja. Versions: deps/versions.sh.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/deps/versions.sh"

OUT="$ROOT/build-deps"				# download cache
WORK="$OUT/windows"
PREFIX="$WORK/install"
TOOLCHAIN="$ROOT/windows/toolchain-mingw-x86_64.cmake"
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

command -v x86_64-w64-mingw32-gcc >/dev/null ||
	{ echo "x86_64-w64-mingw32-gcc not found (brew install mingw-w64)" >&2; exit 1; }
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
		-DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DCMAKE_PREFIX_PATH="$PREFIX" \
		"$@"
	cmake --build "$src/_build" -j"$JOBS"
	cmake --install "$src/_build"
	rm -rf "$src"
}

rm -rf "$PREFIX"
export EF_WIN_PREFIX="$PREFIX"

t="$(fetch "xz-$XZ_VERSION.tar.xz" \
	"https://github.com/tukaani-project/xz/releases/download/v$XZ_VERSION/xz-$XZ_VERSION.tar.xz" "$XZ_SHA256")"
rm -rf "$WORK/xz-$XZ_VERSION" && tar -xJf "$t" -C "$WORK"
cmake_build "$WORK/xz-$XZ_VERSION" -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
	-DXZ_NLS=OFF -DXZ_DOC=OFF -DXZ_TOOL_XZ=OFF -DXZ_TOOL_XZDEC=OFF -DXZ_TOOL_LZMADEC=OFF \
	-DXZ_TOOL_LZMAINFO=OFF -DXZ_TOOL_SYMLINKS=OFF -DXZ_TOOL_SCRIPTS=OFF

t="$(fetch "SDL3-$SDL3_VERSION.tar.gz" \
	"https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" "$SDL3_SHA256")"
rm -rf "$WORK/SDL3-$SDL3_VERSION" && tar -xzf "$t" -C "$WORK"
cmake_build "$WORK/SDL3-$SDL3_VERSION" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF

t="$(fetch "sdl2-compat-$SDL2COMPAT_VERSION.tar.gz" \
	"https://github.com/libsdl-org/sdl2-compat/releases/download/release-$SDL2COMPAT_VERSION/sdl2-compat-$SDL2COMPAT_VERSION.tar.gz" "$SDL2COMPAT_SHA256")"
rm -rf "$WORK/sdl2-compat-$SDL2COMPAT_VERSION" && tar -xzf "$t" -C "$WORK"
cmake_build "$WORK/sdl2-compat-$SDL2COMPAT_VERSION" -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF
# installed as sdl2-compat.pc; "pkg-config sdl2" wants sdl2.pc, as on macOS
ln -sf sdl2-compat.pc "$PREFIX/lib/pkgconfig/sdl2.pc"

# the DLLs must depend on system DLLs only (no libgcc_s / libwinpthread)
bad=0
for dll in "$PREFIX"/bin/*.dll; do
	if x86_64-w64-mingw32-objdump -p "$dll" | grep 'DLL Name' | grep -Eiq 'libgcc|libwinpthread|libstdc'; then
		echo "$dll depends on a mingw runtime DLL" >&2; bad=1
	fi
done
[ "$bad" = 0 ] || exit 1

echo "Built SDL3 $SDL3_VERSION, sdl2-compat $SDL2COMPAT_VERSION, liblzma $XZ_VERSION for Windows x86_64 in $PREFIX"
