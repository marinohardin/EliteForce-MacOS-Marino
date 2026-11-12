#!/bin/sh
# Build Qt 6 (qtbase + qtwayland) for the Linux launcher (ef-launcher), as shared
# libraries the AppImage bundles: Core, Gui, Widgets, DBus, the xcb and wayland
# platform plugins and the gtk3/xdgdesktopportal platform themes.
#
#   linux/build-qt.sh        ->  build-deps/linux/qt/{bin,include,lib,plugins}
#
# Run it inside the Steam Runtime 3 "sniper" SDK container, like build-deps.sh:
#   docker run --rm --platform linux/amd64 -v "$PWD:$PWD" -w "$PWD" -e HOME=/tmp \
#     registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest linux/build-qt.sh
# The sniper SDK has every X11/xcb/xkbcommon/wayland/fontconfig development package
# Qt needs. Kept small on purpose: no OpenGL (the launcher is plain widgets), no ICU,
# no SQL/printing/network TLS. Versions: deps/versions.sh. Slow under emulation.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
. "$ROOT/deps/versions.sh"

OUT="$ROOT/build-deps"
WORK="$OUT/linux"
PREFIX="$WORK/qt"
JOBS="$(nproc)"
mkdir -p "$OUT" "$WORK"

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

QTURL="https://download.qt.io/archive/qt/${QT_VERSION%.*}/$QT_VERSION/submodules"
base="$(fetch "qtbase-everywhere-src-$QT_VERSION.tar.xz" "$QTURL/qtbase-everywhere-src-$QT_VERSION.tar.xz" "$QTBASE_SHA256")"
wl="$(fetch "qtwayland-everywhere-src-$QT_VERSION.tar.xz" "$QTURL/qtwayland-everywhere-src-$QT_VERSION.tar.xz" "$QTWAYLAND_SHA256")"

if [ ! -f "$PREFIX/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" ]; then
	rm -rf "$WORK/qtbase-src" "$WORK/qtbase-build"
	mkdir -p "$WORK/qtbase-src" && tar -xJf "$base" -C "$WORK/qtbase-src" --strip-components=1
	cmake -S "$WORK/qtbase-src" -B "$WORK/qtbase-build" -G Ninja \
		-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
		-DBUILD_SHARED_LIBS=ON -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF \
		-DFEATURE_opengl=OFF -DFEATURE_vulkan=OFF -DFEATURE_icu=OFF \
		-DFEATURE_sql=OFF -DFEATURE_printsupport=OFF -DFEATURE_testlib=ON \
		-DFEATURE_openssl=OFF -DFEATURE_concurrent=OFF \
		-DFEATURE_xcb=ON -DFEATURE_xcb_xlib=ON -DFEATURE_xkbcommon=ON \
		-DFEATURE_dbus=ON -DFEATURE_gtk3=ON -DFEATURE_glib=ON \
		-DFEATURE_fontconfig=ON -DFEATURE_system_freetype=ON \
		-DFEATURE_system_harfbuzz=OFF -DFEATURE_system_png=OFF -DFEATURE_jpeg=OFF \
		-DFEATURE_system_zlib=ON -DFEATURE_system_pcre2=OFF -DFEATURE_system_doubleconversion=OFF
	cmake --build "$WORK/qtbase-build" -j"$JOBS"
	cmake --install "$WORK/qtbase-build"
	rm -rf "$WORK/qtbase-src" "$WORK/qtbase-build"
fi

if [ ! -d "$PREFIX/plugins/platforms" ] || ! ls "$PREFIX"/plugins/platforms/libqwayland*.so >/dev/null 2>&1; then
	rm -rf "$WORK/qtwayland-src" "$WORK/qtwayland-build"
	mkdir -p "$WORK/qtwayland-src" && tar -xJf "$wl" -C "$WORK/qtwayland-src" --strip-components=1
	cmake -S "$WORK/qtwayland-src" -B "$WORK/qtwayland-build" -G Ninja \
		-DCMAKE_PREFIX_PATH="$PREFIX" -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_BUILD_TYPE=Release \
		-DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DFEATURE_wayland_server=OFF
	cmake --build "$WORK/qtwayland-build" -j"$JOBS"
	cmake --install "$WORK/qtwayland-build"
	rm -rf "$WORK/qtwayland-src" "$WORK/qtwayland-build"
fi
chmod -R a+rX "$PREFIX"
echo "Done: $PREFIX"
ls "$PREFIX/plugins/platforms"
