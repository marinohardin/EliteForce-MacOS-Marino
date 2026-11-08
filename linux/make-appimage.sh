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
# build-ffmpeg/linux, see linux/build-deps.sh and linux/build-ffmpeg.sh). The X11 / Wayland /
# audio libraries are dlopened by SDL and the Vulkan loader (libvulkan.so.1) by the
# renderer, all from the host. A final audit fails the build when any ELF file needs a shared
# library that is neither bundled nor on the host allowlist below.
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
		-e NO_BUILD -e VERSION -e CMAKE_PRESET -e CMOD_PAK -e EF_NO_DOCKER=1 \
		-e HOST_UID="$(id -u)" -e HOST_GID="$(id -g)" \
		"$SDK_IMAGE" "$ROOT/linux/make-appimage.sh"
fi

# ---- settings -----------------------------------------------------------------
PRESET="${CMAKE_PRESET:-linux-x64}"
BUILD="$ROOT/build/$PRESET"
DEPS="$ROOT/build-deps/linux/install"
FFMPEG="$ROOT/build-ffmpeg/linux/install"
DIST="$ROOT/dist"
cmake_version() {
	for f in "$ROOT/CMakeLists.txt" "$ROOT/EFAndroid-SP/app/jni/macos/CMakeLists.txt"; do
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
GLIBC_MAX="${GLIBC_MAX:-2.31}"

APPIMAGETOOL_VERSION=1.9.1
APPIMAGETOOL_SHA256=ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0	# from the GitHub release's asset digest

# ---- 1. build -------------------------------------------------------------------
if [ -z "${NO_BUILD:-}" ]; then
	cmake --preset "$PRESET"
	cmake --build "$BUILD" -j"$(nproc)"
fi
for f in efsp libefgame.so libefui.so; do
	[ -f "$BUILD/$f" ] || { echo "$BUILD/$f is missing (build it first; modules must sit next to efsp)" >&2; exit 1; }
done
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

# Copy a NEEDED library from our own dependency prefixes (not the host) into usr/lib,
# recursively. SDL3 is dlopened by sdl2-compat by name, so it is copied explicitly.
needed() { readelf -d "$1" 2>/dev/null | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'; }
bundle() {
	for lib in $(needed "$1"); do
		[ -e "$APPDIR/usr/lib/$lib" ] && continue
		for dir in "$DEPS/lib" "$FFMPEG/lib"; do
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

# rpath $ORIGIN/../lib for the binary, $ORIGIN for the libraries. DT_RPATH (not RUNPATH),
# so it also applies to what sdl2-compat dlopens (libSDL3) and to transitive dependencies.
patchelf --force-rpath --set-rpath '$ORIGIN/../lib' "$APPDIR/usr/bin/efsp"
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

LIC="$APPDIR/usr/share/licenses/eliteforce-marino"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_LICENSES.md" "$ROOT/efgame/STEF Game Source License.doc" "$LIC/"
FF_VERSION="$(sed -n 's/^FFMPEG_VERSION="\(.*\)"/\1/p' "$ROOT/linux/build-ffmpeg.sh" "$ROOT/macos/build-ffmpeg.sh" 2>/dev/null | head -1)"
FF_SRC="$(ls -d "$ROOT"/build-ffmpeg/linux/ffmpeg-*/ "$ROOT"/build-ffmpeg/ffmpeg-*/ 2>/dev/null | head -1)"
[ -n "$FF_SRC" ] && [ -f "${FF_SRC}COPYING.LGPLv2.1" ] && cp "${FF_SRC}COPYING.LGPLv2.1" "$LIC/FFmpeg-LGPL-2.1.txt"
cat > "$LIC/FFmpeg-README.txt" <<EOF_FF
The libav*/libsw* libraries in usr/lib are FFmpeg ${FF_VERSION:-(see THIRD_PARTY_LICENSES.md)}
(https://ffmpeg.org), licensed under the GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
They are unmodified and built from https://ffmpeg.org/releases/ffmpeg-${FF_VERSION:-VERSION}.tar.xz
by linux/build-ffmpeg.sh in the project's source repository, which holds the exact
configure options. You may replace them with your own build of the same version.
EOF_FF
# SDL (zlib license)
for f in "$ROOT/build-deps/linux"/SDL3-*/LICENSE.txt "$ROOT/build-deps/SDL3-"*/LICENSE.txt "$DEPS/share/licenses/SDL3/LICENSE.txt"; do
	[ -f "$f" ] && { cp "$f" "$LIC/SDL3-zlib-LICENSE.txt"; break; }
done
for f in "$ROOT/build-deps/linux"/sdl2-compat-*/LICENSE.txt "$ROOT/build-deps/sdl2-compat-"*/LICENSE.txt; do
	[ -f "$f" ] && { cp "$f" "$LIC/sdl2-compat-zlib-LICENSE.txt"; break; }
done
[ -f "$LIC/SDL3-zlib-LICENSE.txt" ] || echo "Note: SDL's LICENSE.txt not found under build-deps; the zlib license text is not in the AppImage" >&2

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
cp "$ROOT/linux/ReadMe.txt" "$APPDIR/usr/share/eliteforce-marino/ReadMe.txt"
command -v desktop-file-validate >/dev/null 2>&1 && desktop-file-validate "$APPDIR/eliteforce-marino.desktop"

# ---- 4. audit: nothing links outside the bundle except allowed host libraries -----
echo "Auditing shared-library dependencies"
bad=0
glibc_ver_gt() { [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | tail -1)" != "$2" ]; }
for f in "$APPDIR/usr/bin/efsp" "$APPDIR"/usr/lib/*.so*; do
	[ -L "$f" ] && continue
	for lib in $(needed "$f"); do
		[ -e "$APPDIR/usr/lib/$lib" ] && continue
		case " $HOST_OK " in *" $lib "*) continue ;; esac
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
	if ldd "$APPDIR/usr/bin/efsp" 2>&1 | grep -q 'not found'; then
		ldd "$APPDIR/usr/bin/efsp" 2>&1 | grep 'not found' >&2
		echo "error: ldd reports unresolved libraries for efsp" >&2
		bad=1
	fi
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
ARCH=x86_64 "$AIT" --appimage-extract-and-run --no-appstream "$APPDIR" "$OUT"
chmod 755 "$OUT"
if [ -n "${HOST_UID:-}" ]; then
	chown -R "$HOST_UID:$HOST_GID" "$DIST" "$ROOT/build/appimage" 2>/dev/null || true
fi
echo "Done: $OUT ($(du -h "$OUT" | cut -f1))"
