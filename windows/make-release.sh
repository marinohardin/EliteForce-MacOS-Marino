#!/bin/sh
# Cross-build the Windows x64 engine + game modules with mingw-w64 and package them:
#   dist/EliteForce-Marino-<ver>-windows-x64/   (staged folder)
#   dist/EliteForce-Marino-<ver>-windows-x64.zip
#   dist/EliteForce-Marino-<ver>-windows-x64-setup.exe    (only when Inno Setup's iscc is on PATH)
#
#   windows/make-release.sh               build (incremental) + package
#   NO_BUILD=1 windows/make-release.sh    package the existing build/windows-x64 outputs only
#
# Environment: VERSION (default EF_APP_VERSION from the engine's CMakeLists), CMAKE_PRESET
# (windows-x64), CMOD_PAK (default the newest macos/cmod/pakcmod-release-*.pk3), OBJDUMP.
# A final audit fails the build when an .exe/.dll imports a DLL that is neither bundled nor a
# Windows system DLL.
set -eu

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PRESET="${CMAKE_PRESET:-windows-x64}"
BUILD="$ROOT/build/$PRESET"
DEPS="$ROOT/build-deps/windows/install"
FFMPEG="$ROOT/build-ffmpeg/windows/install"
DIST="$ROOT/dist"
OBJDUMP="${OBJDUMP:-x86_64-w64-mingw32-objdump}"
version_from_cmake() {
	for f in "$ROOT/CMakeLists.txt" "$ROOT/EFAndroid-SP/app/jni/macos/CMakeLists.txt"; do
		[ -f "$f" ] && sed -n 's/^.*set(EF_APP_VERSION "\([^"]*\)".*/\1/p' "$f"
	done | head -1
}
VERSION="${VERSION:-$(version_from_cmake)}"
[ -n "$VERSION" ] || { echo "can't tell the version: set VERSION" >&2; exit 1; }
NAME="EliteForce-Marino-$VERSION-windows-x64"
STAGE="$DIST/$NAME"

# ---- 1. build ---------------------------------------------------------------
if [ -z "${NO_BUILD:-}" ]; then
	cmake --preset "$PRESET"
	cmake --build "$BUILD" -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)"
fi
for f in efsp.exe efgame.dll efui.dll; do
	[ -f "$BUILD/$f" ] || { echo "$BUILD/$f is missing (build it first)" >&2; exit 1; }
done

# ---- 2. stage ---------------------------------------------------------------
echo "Staging $STAGE"
rm -rf "$STAGE"
mkdir -p "$STAGE/licenses"
# The engine derives nothing from its executable's name, so it ships as EliteForce.exe
# (that is what Task Manager and the firewall prompt show).
cp "$BUILD/efsp.exe" "$STAGE/EliteForce.exe"
cp "$BUILD/efgame.dll" "$BUILD/efui.dll" "$STAGE/"
$OBJDUMP -h "$STAGE/EliteForce.exe" >/dev/null	# fails early when OBJDUMP is wrong

# Bundled DLLs: SDL3 and sdl2-compat's SDL2 (SDL3.dll is loaded by SDL2.dll by name), and
# every av*/sw* DLL FFmpeg's install has (the minimal LGPL build is just those four).
for f in "$DEPS/bin/SDL3.dll" "$DEPS/bin/SDL2.dll"; do
	[ -f "$f" ] || { echo "$f is missing (windows/build-deps.sh)" >&2; exit 1; }
	cp "$f" "$STAGE/"
done
ffn=0
for f in "$FFMPEG"/bin/av*.dll "$FFMPEG"/bin/sw*.dll; do
	[ -f "$f" ] || continue
	cp "$f" "$STAGE/"
	ffn=$((ffn + 1))
done
[ "$ffn" -gt 0 ] || { echo "no FFmpeg DLLs in $FFMPEG/bin (windows/build-ffmpeg.sh)" >&2; exit 1; }

# cMod's game modules (macos/cmod/README.txt); the engine looks in <exe dir>\cmod
CMOD_PAK="${CMOD_PAK:-$(ls "$ROOT"/macos/cmod/pakcmod-release-*.pk3 2>/dev/null | sort | tail -1)}"
if [ -n "$CMOD_PAK" ] && [ -f "$CMOD_PAK" ]; then
	mkdir -p "$STAGE/cmod"
	cp "$CMOD_PAK" "$ROOT/macos/cmod/README.txt" "$ROOT/efgame/STEF Game Source License.doc" "$STAGE/cmod/"
	echo "cMod game modules: $(basename "$CMOD_PAK")"
else
	echo "Note: no cMod pk3 (macos/cmod/pakcmod-release-*.pk3 or CMOD_PAK): building without cMod's game modules"
fi

# licenses
LIC="$STAGE/licenses"
cp "$ROOT/LICENSE" "$ROOT/THIRD_PARTY_LICENSES.md" "$ROOT/efgame/STEF Game Source License.doc" "$LIC/"
FF_VERSION="$(sed -n 's/^FFMPEG_VERSION="\(.*\)"/\1/p' "$ROOT/windows/build-ffmpeg.sh" "$ROOT/macos/build-ffmpeg.sh" 2>/dev/null | head -1)"
for d in "$ROOT"/build-ffmpeg/windows/ffmpeg-*/ "$ROOT"/build-ffmpeg/ffmpeg-*/; do
	[ -f "${d}COPYING.LGPLv2.1" ] && { cp "${d}COPYING.LGPLv2.1" "$LIC/FFmpeg-LGPL-2.1.txt"; break; }
done
cat > "$LIC/FFmpeg-README.txt" <<EOF_FF
The av*.dll / sw*.dll files next to EliteForce.exe are FFmpeg ${FF_VERSION:-(see THIRD_PARTY_LICENSES.md)}
(https://ffmpeg.org), licensed under the GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
They are unmodified and built from https://ffmpeg.org/releases/ffmpeg-${FF_VERSION:-VERSION}.tar.xz
by windows/build-ffmpeg.sh in the project's source repository, which holds the exact
configure options. You may replace them with your own build of the same version.
EOF_FF
for d in "$ROOT"/build-deps/windows/SDL3-*/ "$ROOT"/build-deps/SDL3-*/; do
	[ -f "${d}LICENSE.txt" ] && { cp "${d}LICENSE.txt" "$LIC/SDL3-zlib-LICENSE.txt"; break; }
done
for d in "$ROOT"/build-deps/windows/sdl2-compat-*/ "$ROOT"/build-deps/sdl2-compat-*/; do
	[ -f "${d}LICENSE.txt" ] && { cp "${d}LICENSE.txt" "$LIC/sdl2-compat-zlib-LICENSE.txt"; break; }
done
[ -f "$LIC/SDL3-zlib-LICENSE.txt" ] || echo "Note: SDL's LICENSE.txt not found under build-deps; the zlib license text is not packaged" >&2

# Read Me.txt with CRLF line endings for Notepad
sed 's/$/\r/' "$ROOT/windows/ReadMe.txt" > "$STAGE/Read Me.txt"

# ---- 3. audit: imports are bundled or Windows system DLLs -----------------------------
echo "Auditing DLL imports"
SYSTEM_DLLS=' kernel32 user32 gdi32 gdi32full advapi32 shell32 ole32 oleaut32 ws2_32 winmm iphlpapi dbghelp psapi setupapi imm32 version shlwapi msvcrt ucrtbase bcrypt bcryptprimitives cfgmgr32 comctl32 comdlg32 combase crypt32 secur32 shcore wsock32 mswsock dwmapi uxtheme userenv winspool rpcrt4 ntdll normaliz mf mfplat mfreadwrite mfuuid dxgi d3d11 d3d12 dinput8 xinput1_4 xinput9_1_0 hid winhttp wininet wldap32 ncrypt netapi32 sensorsapi propsys windowscodecs cabinet vulkan-1 '
bad=0
for f in "$STAGE"/*.exe "$STAGE"/*.dll; do
	for dll in $($OBJDUMP -p "$f" | sed -n 's/^[[:space:]]*DLL Name: \(.*\)$/\1/p'); do
		l="$(printf '%s' "$dll" | tr 'A-Z' 'a-z')"
		[ -f "$STAGE/$dll" ] && continue
		base="${l%.dll}"
		case "$base" in api-ms-win-*|ext-ms-win-*) continue ;; esac
		case "$SYSTEM_DLLS" in *" $base "*) continue ;; esac
		echo "error: $(basename "$f") imports $dll, which is neither bundled nor an allowed system DLL" >&2
		bad=1
	done
done
[ "$bad" = 0 ] || { echo "import audit failed" >&2; exit 1; }

# ---- 4. zip -----------------------------------------------------------------------
ZIP="$DIST/$NAME.zip"
rm -f "$ZIP"
( cd "$DIST" && zip -qr -X "$ZIP" "$NAME" )
echo "Done: $ZIP ($(du -h "$ZIP" | cut -f1))"

# ---- 5. installer (optional) --------------------------------------------------------
if command -v iscc >/dev/null 2>&1; then
	iscc "/DAppVersion=$VERSION" "/DSourceDir=$STAGE" "/O$DIST" "/F$NAME-setup" "$ROOT/windows/installer.iss"
	echo "Done: $DIST/$NAME-setup.exe"
else
	echo "iscc (Inno Setup) not found: skipping the installer"
fi
