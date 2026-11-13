#!/bin/sh
# Build the Linux x86_64 engine + game modules and package them as an AppImage:
#   dist/EliteForce-Marino-<ver>-x86_64.AppImage
#
#   linux/make-appimage.sh               build (incremental) + package
#   NO_BUILD=1 linux/make-appimage.sh    package the existing build/linux-x64 outputs only
#
# Run it on a Linux x86_64 host, or anywhere with Docker: outside the container it
# re-runs itself inside Valve's Steam Runtime 3 "sniper" SDK image (linux/amd64), the
# oldest glibc we support (2.31), which is what makes the AppImage run on Steam Deck
# and on most desktop distributions. Set EF_NO_DOCKER=1 to skip that and use this host.
#
# The AppImage bundles only what the host can't be trusted to have: the engine, the
# game modules, SDL3 + sdl2-compat and FFmpeg's libav*/libsw* (from build-deps/linux and
# build-ffmpeg/linux, see linux/build-deps.sh and linux/build-ffmpeg.sh), and for the launcher
# (ef-launcher, Qt 6, src efcode/sys/qt) Qt's libraries and plugins (build-deps/linux/qt, see
# linux/build-qt.sh) with the xcb helper libraries hosts don't reliably have (libxcb-cursor0,
# libxkbcommon-x11, ...). The X11 / Wayland / audio libraries are dlopened by SDL and the Vulkan
# loader (libvulkan.so.1) by the renderer, all from the host. A final audit fails the build when
# any ELF file needs a shared library that is neither bundled nor on the host allowlist below.
#
# Environment: VERSION (default EF_APP_VERSION from the engine's CMakeLists), CMAKE_PRESET
# (linux-x64), CMOD_PAK (default the newest macos/cmod/pakcmod-release-*.pk3), SDK_IMAGE.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK_IMAGE="${SDK_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk}"

# ---- 0. enter the container ---------------------------------------------------
if [ -z "${EF_NO_DOCKER:-}" ] && [ ! -f /.dockerenv ] && [ -z "${container:-}" ]; then
	command -v docker >/dev/null 2>&1 || { echo "docker not found (or set EF_NO_DOCKER=1 to build on this host)" >&2; exit 1; }
	exec docker run --rm --platform linux/amd64 \
		-v "$ROOT:$ROOT" -w "$ROOT" \
		-e NO_BUILD -e REQUIRE_LAUNCHER -e VERSION -e CMAKE_PRESET -e CMOD_PAK -e EF_NO_DOCKER=1 \
		-e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
		"$SDK_IMAGE" "$ROOT/linux/make-appimage.sh"
fi

# ---- settings -----------------------------------------------------------------
PRESET="${CMAKE_PRESET:-linux-x64}"
BUILD="$ROOT/build/$PRESET"
DEPS="$ROOT/build-deps/linux/install"
FFMPEG="$ROOT/build-ffmpeg/linux/install"
QT="$ROOT/build-deps/linux/qt"				# Qt 6 (linux/build-qt.sh); without it the AppImage has no launcher
HAVE_QT=0
[ -f "$QT/lib/cmake/Qt6Widgets/Qt6WidgetsConfig.cmake" ] && HAVE_QT=1
DIST="$ROOT/dist"
cmake_version() {
	for f in "$ROOT/CMakeLists.txt" "$ROOT/EFAndroid-SP/app/jni/engine/CMakeLists.txt" "$ROOT/EFAndroid-SP/app/jni/macos/CMakeLists.txt"; do
		[ -f "$f" ] && sed -n 's/^.*set(EF_APP_VERSION "\([^"]*\)".*/\1/p' "$f" | head -1
	done | head -1
}
VERSION="${VERSION:-$(cmake_version)}"
[ -n "$VERSION" ] || { echo "can't tell the version: set VERSION" >&2; exit 1; }
OUT="$DIST/EliteForce-Marino-$VERSION-x86_64.AppImage"
APPDIR="$ROOT/build/appimage/AppDir"
TOOLS="$ROOT/build/appimage/tools"

# Host libraries an ELF file in the AppDir may need (everything else must be bundled).
# libstdc++ / libgcc_s are tolerated with a warning: the build should link them statically.
HOST_OK='ld-linux-x86-64.so.2 libc.so.6 libm.so.6 libdl.so.2 libpthread.so.0 librt.so.1 libresolv.so.2 libutil.so.1'
HOST_WARN='libstdc++.so.6 libgcc_s.so.1'
# What Qt's libraries and plugins may take from the host: ubiquitous desktop libraries (X11 core,
# fontconfig / freetype, glib, dbus, wayland client) and, for the optional GTK platform theme plugin
# only, GTK itself (the plugin simply doesn't load on a host without GTK 3).
HOST_OK_QT='libX11.so.6 libxcb.so.1 libXau.so.6 libXdmcp.so.6 libSM.so.6 libICE.so.6 libfontconfig.so.1 libfreetype.so.6 libz.so.1 libglib-2.0.so.0 libgobject-2.0.so.0 libgio-2.0.so.0 libgmodule-2.0.so.0 libdbus-1.so.3 libwayland-client.so.0 libwayland-cursor.so.0 libexpat.so.1 libuuid.so.1 libpcre2-8.so.0 libffi.so.7 libmount.so.1 libselinux.so.1'
HOST_OK_GTK='libgtk-3.so.0 libgdk-3.so.0 libpango-1.0.so.0 libgdk_pixbuf-2.0.so.0 libcairo.so.2 libatk-1.0.so.0 libpangocairo-1.0.so.0'
# helper libraries Qt's xcb platform plugin needs that hosts don't reliably have, copied from the build
# container (the Steam Runtime SDK's own libraries, so the glibc requirement is the same)
SYS_BUNDLE='libxcb-cursor.so.0 libxcb-icccm.so.4 libxcb-image.so.0 libxcb-keysyms.so.1 libxcb-randr.so.0 libxcb-render.so.0 libxcb-render-util.so.0 libxcb-shape.so.0 libxcb-shm.so.0 libxcb-sync.so.1 libxcb-xfixes.so.0 libxcb-xkb.so.1 libxcb-util.so.1 libxcb-xinerama.so.0 libxkbcommon.so.0 libxkbcommon-x11.so.0 libX11-xcb.so.1 libbrotlidec.so.1 libbrotlicommon.so.1'
SYS_LIBDIR=/usr/lib/x86_64-linux-gnu
GLIBC_MAX="${GLIBC_MAX:-2.31}"

APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0	# from the GitHub release's asset digest

# ---- 1. build -------------------------------------------------------------------
if [ -z "${NO_BUILD:-}" ]; then
	# the version shown in the game (console, menus, server browsers), as make-app.sh does on macOS
	cmake --preset "$PRESET" -DEF_APP_VERSION="$VERSION"
	cmake --build "$BUILD" -j"$(nproc)"
fi
for f in efsp libefgame.so libefui.so; do
	[ -f "$BUILD/$f" ] || { echo "$BUILD/$f is missing (build it first; modules must sit next to efsp)" >&2; exit 1; }
done
if [ "$HAVE_QT" = 1 ]; then
	[ -f "$BUILD/ef-launcher" ] || { echo "$BUILD/ef-launcher is missing (Qt is in $QT: configure with the linux-x64 preset again)" >&2; exit 1; }
elif [ -n "${REQUIRE_LAUNCHER:-}" ]; then
	echo "No Qt in $QT, so no launcher: run linux/build-qt.sh first (REQUIRE_LAUNCHER is set)" >&2
	exit 1
else
	echo "Note: no Qt in $QT (linux/build-qt.sh): building the AppImage without the launcher"
fi
# the sniper SDK image has no patchelf; install it when we are root in a container
if ! command -v patchelf >/dev/null 2>&1 && [ "$(id -u)" = 0 ] && command -v apt-get >/dev/null 2>&1; then
	apt-get update -qq >/dev/null && apt-get install -y -qq patchelf >/dev/null
fi
command -v patchelf >/dev/null 2>&1 || { echo "patchelf is needed (apt-get install patchelf)" >&2; exit 1; }

# ---- 2. AppDir ------------------------------------------------------------------
echo "Assembling $OUT"
rm -rf "$APPDIR"
mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib" "$APPDIR/usr/share/eliteforce-marino" \
	"$APPDIR/usr/share/licenses/eliteforce-marino" "$DIST"

cp "$BUILD/efsp" "$APPDIR/usr/bin/efsp"
# Sys_BinaryPath() is usr/bin; the game modules are loaded from the executable's directory
# (sp/sp_clink.c), so they live next to it in the build tree. Keep them in usr/lib and
# link them next to the binary so both places work.
cp "$BUILD/libefgame.so" "$BUILD/libefui.so" "$APPDIR/usr/lib/"
ln -s ../lib/libefgame.so "$APPDIR/usr/bin/libefgame.so"
ln -s ../lib/libefui.so "$APPDIR/usr/bin/libefui.so"
strip --strip-unneeded "$APPDIR/usr/bin/efsp" "$APPDIR/usr/lib/libefgame.so" "$APPDIR/usr/lib/libefui.so" 2>/dev/null || true
if [ "$HAVE_QT" = 1 ]; then
	cp "$BUILD/ef-launcher" "$APPDIR/usr/bin/ef-launcher"
	strip --strip-unneeded "$APPDIR/usr/bin/ef-launcher" 2>/dev/null || true
fi

# Copy a NEEDED library from our own dependency prefixes (not the host) into usr/lib,
# recursively. SDL3 is dlopened by sdl2-compat by name, so it is copied explicitly.
needed() { readelf -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
bundle() {
	for lib in $(needed "$1"); do
		[ -e "$APPDIR/usr/lib/$lib" ] && continue
		for dir in "$DEPS/lib" "$FFMPEG/lib" "$QT/lib"; do
			if [ -e "$dir/$lib" ]; then
				cp -L "$dir/$lib" "$APPDIR/usr/lib/$lib"
				chmod u+w "$APPDIR/usr/lib/$lib"
				bundle "$APPDIR/usr/lib/$lib"
				break
			fi
		done
	done
}
for f in "$APPDIR/usr/bin/efsp" "$APPDIR/usr/lib/libefgame.so" "$APPDIR/usr/lib/libefui.so"; do bundle "$f"; done
for lib in libSDL3.so.0; do
	[ -e "$DEPS/lib/$lib" ] || { echo "$DEPS/lib/$lib is missing (linux/build-deps.sh)" >&2; exit 1; }
	[ -e "$APPDIR/usr/lib/$lib" ] || { cp -L "$DEPS/lib/$lib" "$APPDIR/usr/lib/$lib"; chmod u+w "$APPDIR/usr/lib/$lib"; bundle "$APPDIR/usr/lib/$lib"; }
done

# ---- the launcher: Qt libraries, plugins, qt.conf ----------------------------------------------
if [ "$HAVE_QT" = 1 ]; then
	bundle "$APPDIR/usr/bin/ef-launcher"
	# plugins: platforms (xcb for X11 / XWayland / gamescope, wayland, offscreen for tests), platform
	# themes (follow GNOME / KDE: dark mode, accent), input methods, Wayland shell integration
	for pl in platforms/libqxcb.so platforms/libqwayland-generic.so platforms/libqoffscreen.so \
		platformthemes/libqgtk3.so platformthemes/libqxdgdesktopportal.so \
		platforminputcontexts/libcomposeplatforminputcontextplugin.so platforminputcontexts/libibusplatforminputcontextplugin.so \
		imageformats/libqgif.so imageformats/libqico.so; do
		[ -f "$QT/plugins/$pl" ] || { echo "$QT/plugins/$pl is missing (linux/build-qt.sh)" >&2; exit 1; }
		mkdir -p "$APPDIR/usr/plugins/$(dirname "$pl")"
		cp "$QT/plugins/$pl" "$APPDIR/usr/plugins/$pl"
	done
	for d in wayland-shell-integration wayland-decoration-client; do
		mkdir -p "$APPDIR/usr/plugins/$d"
		cp "$QT/plugins/$d"/*.so "$APPDIR/usr/plugins/$d/"
	done
	chmod -R u+w "$APPDIR/usr/plugins"
	# libraries the plugins need from our prefixes (Qt's own), then the xcb helpers from the container
	for f in "$APPDIR"/usr/plugins/*/*.so; do bundle "$f"; done
	for lib in $SYS_BUNDLE; do
		[ -e "$APPDIR/usr/lib/$lib" ] && continue
		if [ -e "$SYS_LIBDIR/$lib" ]; then
			cp -L "$SYS_LIBDIR/$lib" "$APPDIR/usr/lib/$lib"
			chmod u+w "$APPDIR/usr/lib/$lib"
		else
			echo "Note: $SYS_LIBDIR/$lib not found: not bundled" >&2
		fi
	done
	# qt.conf next to the binary: plugins and libraries inside the AppImage, whatever the host has
	cat > "$APPDIR/usr/bin/qt.conf" <<'EOF_QTCONF'
[Paths]
Prefix = ..
Plugins = plugins
Libraries = lib
EOF_QTCONF
	for pf in "$APPDIR"/usr/plugins/*/*.so; do
		strip --strip-unneeded "$pf" 2>/dev/null || true
		patchelf --force-rpath --set-rpath '$ORIGIN/../../lib' "$pf"
	done
	strip --strip-unneeded "$APPDIR"/usr/lib/libQt6*.so.* 2>/dev/null || true
fi

# rpath $ORIGIN/../lib for the binary, $ORIGIN for the libraries. DT_RPATH (not RUNPATH),
# so it also applies to what sdl2-compat dlopens (libSDL3) and to transitive dependencies.
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/efsp"
[ "$HAVE_QT" = 1 ] && patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/ef-launcher"
for f in "$APPDIR"/usr/lib/*.so*; do
	[ -L "$f" ] || patchelf --force-rpath --set-rpath '$ORIGIN' "$f"
done

# ---- 3. cMod, licenses, launcher files -------------------------------------------
# cMod's game modules (macos/cmod/README.txt); the engine looks in
# <exe dir>/../share/eliteforce-marino/cmod (Linux counterpart of Contents/Resources/cmod)
CMOD_PAK="${CMOD_PAK:-$(ls "$ROOT"/macos/cmod/pakcmod-release-*.pk3 2>/dev/null | sort | tail -1)}"
if [ -n "$CMOD_PAK" ] && [ -f "$CMOD_PAK" ]; then
	mkdir -p "$APPDIR/usr/share/eliteforce-marino/cmod"
	cp "$CMOD_PAK" "$ROOT/macos/cmod/README.txt" "$ROOT/efgame/STEF Game Source License.doc" "$APPDIR/usr/share/eliteforce-marino/cmod/"
	echo "cMod game modules: $(basename "$CMOD_PAK")"
else
	echo "Note: no cMod pk3 (macos/cmod/pakcmod-release-*.pk3 or CMOD_PAK): building without cMod's game modules"
fi

# Every notice the AppImage must carry. A missing one fails the build (no release without
# its license texts).
LIC="$APPDIR/usr/share/licenses/eliteforce-marino"
# lic_copy DEST SRC...: copy the first SRC that exists to $LIC/DEST, or fail
lic_copy() {
	dest="$1"; shift
	for src in "$@"; do
		[ -f "$src" ] && { mkdir -p "$(dirname "$LIC/$dest")"; cp "$src" "$LIC/$dest"; return 0; }
	done
	echo "error: license text for $dest not found (looked for: $*)" >&2
	exit 1
}
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_LICENSES.md" "$ROOT/efgame/STEF Game Source License.doc" "$LIC/"
FF_VERSION="$(sed -n 's/^FFMPEG_VERSION="\(.*\)"/\1/p' "$ROOT/deps/versions.sh" 2>/dev/null | head -1)"	# shared pins
# FFmpeg: installed by linux/build-ffmpeg.sh (older FFmpeg builds kept their source tree instead)
lic_copy FFmpeg-LGPL-2.1.txt "$FFMPEG/licenses/FFmpeg-COPYING.LGPLv2.1.txt" \
	"$ROOT/build-ffmpeg/linux/ffmpeg-$FF_VERSION/COPYING.LGPLv2.1" "$ROOT/build-ffmpeg/ffmpeg-$FF_VERSION/COPYING.LGPLv2.1"
cat > "$LIC/FFmpeg-README.txt" <<EOF_FF
The libav*/libsw* libraries in usr/lib are FFmpeg ${FF_VERSION:-(see THIRD_PARTY_LICENSES.md)}
(https://ffmpeg.org), licensed under the GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
They are unmodified and built from https://ffmpeg.org/releases/ffmpeg-${FF_VERSION:-VERSION}.tar.xz
by linux/build-ffmpeg.sh in the project's source repository, which holds the exact
configure options. You may replace them with your own build of the same version.
EOF_FF
# SDL3 and sdl2-compat (zlib license), installed by their CMake builds
lic_copy SDL3-zlib-LICENSE.txt "$DEPS/share/licenses/SDL3/LICENSE.txt"
lic_copy sdl2-compat-zlib-LICENSE.txt "$DEPS/share/licenses/sdl2-compat/LICENSE.txt"
# liblzma (static, in efsp / ef-launcher: GOG installer extraction): 0BSD, plus XZ Utils' licensing summary
lic_copy liblzma-0BSD.txt "$DEPS/licenses/xz-COPYING.0BSD.txt"
lic_copy XZ-Utils-COPYING.txt "$DEPS/licenses/xz-COPYING.txt"
# The AppImage runtime (the ELF stub appimagetool puts in front of the squashfs image, from
# github.com/AppImage/type2-runtime): MIT, statically linked with musl, libfuse (LGPL-2.1),
# squashfuse, zstd and zlib; their texts are kept in linux/licenses/appimage-runtime.
for f in "$ROOT"/linux/licenses/appimage-runtime/*.txt; do
	lic_copy "AppImage-runtime/$(basename "$f")" "$f"
done
[ -f "$LIC/AppImage-runtime/type2-runtime-LICENSE.txt" ] || { echo "error: linux/licenses/appimage-runtime is incomplete" >&2; exit 1; }

if [ "$HAVE_QT" = 1 ]; then
	# Qt is LGPL-3.0: the license texts and the note on where the source is (THIRD_PARTY_LICENSES.md)
	cp "$ROOT/linux/licenses/Qt-LGPL-3.0.txt" "$ROOT/linux/licenses/GPL-3.0.txt" "$LIC/"
	QT_VERSION="$(sed -n 's/^QT_VERSION="\{0,1\}\([0-9.]*\)"\{0,1\}.*/\1/p' "$ROOT/deps/versions.sh" | head -1)"
	cat > "$LIC/Qt-README.txt" <<EOF_QT
ef-launcher (the launcher window) uses Qt ${QT_VERSION:-6} (QtCore, QtGui, QtWidgets, QtNetwork, QtDBus,
QtWaylandClient and the xcb / Wayland platform plugins), licensed under the GNU LGPL 3.0 (Qt-LGPL-3.0.txt, which
refers to the GPL 3.0 in GPL-3.0.txt). Qt is unmodified: it is built from the official source tarballs
https://download.qt.io/archive/qt/${QT_VERSION%.*}/${QT_VERSION:-6}/submodules/qtbase-everywhere-src-${QT_VERSION:-6}.tar.xz and
qtwayland-everywhere-src-${QT_VERSION:-6}.tar.xz by linux/build-qt.sh in the project's source repository, which holds the exact
configure options. The libraries are shared (usr/lib/libQt6*.so*, usr/plugins): you may replace them with your own
build of the same Qt version.
EOF_QT
fi
# The xcb / xkbcommon / brotli helper libraries copied from the build container ($SYS_BUNDLE, Steam
# Runtime packages): each package's Debian copyright file, which holds its license text.
for lib in $SYS_BUNDLE; do
	[ -e "$APPDIR/usr/lib/$lib" ] || continue
	[ -e "$SYS_LIBDIR/$lib" ] || continue	# bundled from one of our own prefixes, not the container
	command -v dpkg >/dev/null 2>&1 || { echo "error: dpkg is needed to find the license of $lib" >&2; exit 1; }
	pkg="$(dpkg -S "$SYS_LIBDIR/$lib" 2>/dev/null | head -1 | cut -d: -f1)"
	[ -n "$pkg" ] || pkg="$(dpkg -S "$(readlink -f "$SYS_LIBDIR/$lib")" 2>/dev/null | head -1 | cut -d: -f1)"
	[ -n "$pkg" ] || { echo "error: no package owns $SYS_LIBDIR/$lib (its license can't be found)" >&2; exit 1; }
	mkdir -p "$LIC/steam-runtime"
	[ -f "$LIC/steam-runtime/libraries.txt" ] ||
		echo "usr/lib library: Steam Runtime (Debian) package whose <package>-copyright.txt applies" > "$LIC/steam-runtime/libraries.txt"
	echo "$lib: $pkg" >> "$LIC/steam-runtime/libraries.txt"
	[ -f "$LIC/steam-runtime/$pkg-copyright.txt" ] && continue
	lic_copy "steam-runtime/$pkg-copyright.txt" "/usr/share/doc/$pkg/copyright"
done
cp "$ROOT/linux/deck.cfg" "$APPDIR/usr/share/eliteforce-marino/deck.cfg"
cp "$ROOT/linux/AppRun" "$APPDIR/AppRun"
chmod 755 "$APPDIR/AppRun"
cp "$ROOT/linux/eliteforce-marino.desktop" "$APPDIR/eliteforce-marino.desktop"
mkdir -p "$APPDIR/usr/share/applications" "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$ROOT/linux/eliteforce-marino.desktop" "$APPDIR/usr/share/applications/"
ICON="$ROOT/linux/eliteforce-marino.png"	# 256px, made by icon/make-icons.sh
[ -f "$ICON" ] || { echo "$ICON is missing (run icon/make-icons.sh on a Mac)" >&2; exit 1; }
cp "$ICON" "$APPDIR/eliteforce-marino.png"
cp "$ICON" "$APPDIR/.DirIcon"
cp "$ICON" "$APPDIR/usr/share/icons/hicolor/256x256/apps/eliteforce-marino.png"
sed "s/@VERSION@/$VERSION/g" "$ROOT/linux/ReadMe.txt" > "$APPDIR/usr/share/eliteforce-marino/ReadMe.txt"
command -v desktop-file-validate >/dev/null 2>&1 && desktop-file-validate "$APPDIR/eliteforce-marino.desktop"

# ---- 4. audit: nothing links outside the bundle except allowed host libraries -----
echo "Auditing shared-library dependencies"
bad=0
glibc_ver_gt() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" != "$2" ]; }
AUDIT_FILES="$APPDIR/usr/bin/efsp $APPDIR/usr/bin/ef-launcher $(ls "$APPDIR"/usr/lib/*.so* "$APPDIR"/usr/plugins/*/*.so 2>/dev/null | tr '\n' ' ')"
for f in $AUDIT_FILES; do
	[ -L "$f" ] && continue
	[ -f "$f" ] || continue
	isqt=0
	case "$f" in */libQt6*|*/usr/plugins/*|*/ef-launcher) isqt=1 ;; esac
	case " $SYS_BUNDLE " in *" $(basename "$f") "*) isqt=1 ;; esac
	for lib in $(needed "$f"); do
		[ -e "$APPDIR/usr/lib/$lib" ] && continue
		case " $HOST_OK " in *" $lib "*) continue ;; esac
		if [ "$isqt" = 1 ]; then
			case " $HOST_OK_QT " in *" $lib "*) continue ;; esac
			case "$f" in */libqgtk3.so) case " $HOST_OK_GTK " in *" $lib "*) continue ;; esac ;; esac
		fi
		case " $HOST_WARN " in
			*" $lib "*) echo "warning: $(basename "$f") needs host $lib (link libstdc++/libgcc statically)" >&2; continue ;;
		esac
		echo "error: $(basename "$f") needs $lib, which is neither bundled nor an allowed host library" >&2
		bad=1
	done
	# glibc symbol versions: the AppImage must run on the oldest glibc we support
	newest="$(objdump -T "$f" 2>/dev/null | sed -n 's/.*GLIBC_\([0-9][0-9.]*\).*/\1/p' | sort -V | tail -1)"
	if [ -n "$newest" ] && glibc_ver_gt "$newest" "$GLIBC_MAX"; then
		echo "error: $(basename "$f") needs GLIBC_$newest (> $GLIBC_MAX)" >&2
		bad=1
	fi
done
# every NEEDED the dynamic loader can see must resolve (catches a missing transitive lib)
if command -v ldd >/dev/null 2>&1; then
	for exe in efsp ef-launcher; do
		[ -f "$APPDIR/usr/bin/$exe" ] || continue
		if ldd "$APPDIR/usr/bin/$exe" 2>&1 | grep -q 'not found'; then
			ldd "$APPDIR/usr/bin/$exe" 2>&1 | grep 'not found' >&2
			echo "error: ldd reports unresolved libraries for $exe" >&2
			bad=1
		fi
	done
fi
[ "$bad" = 0 ] || { echo "dependency audit failed" >&2; exit 1; }

# ---- 5. appimagetool ---------------------------------------------------------------
mkdir -p "$TOOLS"
AIT="$TOOLS/appimagetool-$APPIMAGETOOL_VERSION-x86_64.AppImage"
if [ ! -x "$AIT" ]; then
	curl -fL -o "$AIT.part" "https://github.com/AppImage/appimagetool/releases/download/$APPIMAGETOOL_VERSION/appimagetool-x86_64.AppImage"
	got="$(sha256sum "$AIT.part" | awk '{print $1}')"
	[ "$got" = "$APPIMAGETOOL_SHA256" ] || { rm -f "$AIT.part"; echo "appimagetool checksum mismatch ($got)" >&2; exit 1; }
	chmod +x "$AIT.part"
	mv "$AIT.part" "$AIT"
fi
rm -f "$OUT"
# --appimage-extract-and-run: no FUSE inside the container. appimagetool 1.9 embeds the
# static type-2 runtime, so the result needs no libfuse2 beyond what the user's system has.
# Run a copy with the AppImage magic ("AI\002" at ELF offset 8) zeroed: emulated amd64
# containers (Rosetta, qemu-user binfmt on Apple Silicon) refuse to exec it otherwise
# ("Exec format error"). Native Linux doesn't care either way.
AIT_RUN="$TOOLS/appimagetool-run"
cp "$AIT" "$AIT_RUN"
printf '\000\000\000' | dd of="$AIT_RUN" bs=1 seek=8 count=3 conv=notrunc 2>/dev/null
chmod +x "$AIT_RUN"
ARCH=x86_64 "$AIT_RUN" --appimage-extract-and-run --no-appstream "$APPDIR" "$OUT"
chmod 755 "$OUT"
if [ -n "${HOST_UID:-}" ]; then
	chown -R "$HOST_UID:$HOST_GID" "$DIST" "$ROOT/build/appimage" 2>/dev/null || true
fi
echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"
