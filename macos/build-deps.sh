#!/bin/sh
# Build the libraries the app bundles (other than FFmpeg, see build-ffmpeg.sh and
# MoltenVK, whose Homebrew build already supports macOS 12) for the app's minimum
# macOS, instead of copying Homebrew's, which target the build Mac's macOS:
#   SDL3, sdl2-compat (the SDL2 API the engine uses, on SDL3) and the Vulkan loader.
#
#   macos/build-deps.sh        ->  build-deps/install/{include,lib}
#
# MIN_MACOS (default 12.0) is the deployment target; make-app.sh passes its own.
# The versions and checksums match Homebrew's formulae (the same releases the
# app used to bundle); bump them together.
set -eu

MIN_MACOS="${MIN_MACOS:-12.0}"
SDL3_VERSION="3.4.18"
SDL3_SHA256="9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3"
SDL2COMPAT_VERSION="2.32.74"
SDL2COMPAT_SHA256="ec68abde77e2e459c8abc4f9587b8976b5d2a657b2bfda2fb3a079b0f8924588"
VULKAN_VERSION="1.4.363.0"
VULKAN_SHA256="941eff558fc74f248745fb7df367640cf89a8457f216dae498a74ae14b6ba6f7"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/build-deps"
PREFIX="$OUT/install"
BREW="${BREW:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"
JOBS="$(sysctl -n hw.ncpu)"

mkdir -p "$OUT"

# fetch NAME URL SHA256: download (once) and verify; prints the tarball path
fetch() {
	tarball="$OUT/$1"
	if [ ! -f "$tarball" ]; then
		echo "Downloading $1" >&2
		curl -fL -o "$tarball.part" "$2"
		mv "$tarball.part" "$tarball"
	fi
	echo "$3  $tarball" | shasum -a 256 -c - >/dev/null ||
		{ echo "checksum mismatch for $tarball" >&2; exit 1; }
	echo "$tarball"
}

# cmake_build SRCDIR [cmake options...]: configure, build and install into PREFIX,
# then drop the build tree (disk space)
cmake_build() {
	src="$1"; shift
	cmake -S "$src" -B "$src/_build" -G Ninja \
		-DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_OSX_ARCHITECTURES=arm64 \
		-DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS" \
		-DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DCMAKE_PREFIX_PATH="$PREFIX" \
		"$@"
	cmake --build "$src/_build" -j"$JOBS"
	cmake --install "$src/_build"
	rm -rf "$src"
}

rm -rf "$PREFIX"
export MACOSX_DEPLOYMENT_TARGET="$MIN_MACOS"

t="$(fetch "SDL3-$SDL3_VERSION.tar.gz" \
	"https://github.com/libsdl-org/SDL/releases/download/release-$SDL3_VERSION/SDL3-$SDL3_VERSION.tar.gz" "$SDL3_SHA256")"
rm -rf "$OUT/SDL3-$SDL3_VERSION" && tar -xzf "$t" -C "$OUT"
cmake_build "$OUT/SDL3-$SDL3_VERSION" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF

t="$(fetch "sdl2-compat-$SDL2COMPAT_VERSION.tar.gz" \
	"https://github.com/libsdl-org/sdl2-compat/releases/download/release-$SDL2COMPAT_VERSION/sdl2-compat-$SDL2COMPAT_VERSION.tar.gz" "$SDL2COMPAT_SHA256")"
rm -rf "$OUT/sdl2-compat-$SDL2COMPAT_VERSION" && tar -xzf "$t" -C "$OUT"
cmake_build "$OUT/sdl2-compat-$SDL2COMPAT_VERSION" -DSDL2COMPAT_TESTS=OFF -DSDL2COMPAT_STATIC=OFF
# installed as sdl2-compat.pc; "pkg-config sdl2" (the engine's CMake) wants sdl2.pc, as Homebrew links it
ln -sf sdl2-compat.pc "$PREFIX/lib/pkgconfig/sdl2.pc"

# the loader needs the Vulkan headers of the same version (Homebrew's vulkan-headers)
t="$(fetch "Vulkan-Loader-$VULKAN_VERSION.tar.gz" \
	"https://github.com/KhronosGroup/Vulkan-Loader/archive/refs/tags/vulkan-sdk-$VULKAN_VERSION.tar.gz" "$VULKAN_SHA256")"
rm -rf "$OUT/Vulkan-Loader-vulkan-sdk-$VULKAN_VERSION" && tar -xzf "$t" -C "$OUT"
cmake_build "$OUT/Vulkan-Loader-vulkan-sdk-$VULKAN_VERSION" -DVULKAN_HEADERS_INSTALL_DIR="$BREW" -DBUILD_TESTS=OFF

echo "Built SDL3 $SDL3_VERSION, sdl2-compat $SDL2COMPAT_VERSION, Vulkan loader $VULKAN_VERSION for macOS >= $MIN_MACOS in $PREFIX"
