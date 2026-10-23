#!/bin/sh
# Build the macOS engine + SP game modules and assemble a self-contained,
# ad-hoc signed app bundle:  dist/EliteForceSP.app
#
#   macos/make-app.sh               build (incremental) + bundle
#   NO_BUILD=1 macos/make-app.sh    bundle the existing build outputs only
#
# Needs Xcode command line tools, cmake, pkg-config and the Homebrew packages
# used by the dev build: sdl2-compat (+ sdl3), ffmpeg, molten-vk, vulkan-loader.
# Every non-system dylib (and its transitive deps) is copied into
# Contents/Frameworks and relinked to @rpath, so the .app runs without Homebrew.
# The game data is NOT bundled (see Sys_MacCheckGameData in efcode/sys/sys_osx.m).
set -eu

APP_NAME="${APP_NAME:-EliteForceSP}"
DISPLAY_NAME="${DISPLAY_NAME:-Elite Force SP}"
BUNDLE_ID="${BUNDLE_ID:-org.voyagersp.macos}"
case "$APP_NAME" in *" "*) echo "APP_NAME must not contain spaces (use DISPLAY_NAME)" >&2; exit 1 ;; esac
BREW="${BREW:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_BUILD="$ROOT/build-macos"
GAME_BUILD="$ROOT/efgame/buildmac"
DIST="$ROOT/dist"
APP="$DIST/$APP_NAME.app"
VERSION="$(sed -n 's/.*versionName "\(.*\)".*/\1/p' "$ROOT/EFAndroid-SP/app/build.gradle" | head -1)"
VERSION="${VERSION:-1.0}"
BUILD="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)"

# ---- 1. build ---------------------------------------------------------------
if [ -z "${NO_BUILD:-}" ]; then
	cmake -S "$ROOT/efgame" -B "$GAME_BUILD" -DCMAKE_OSX_ARCHITECTURES=arm64
	cmake --build "$GAME_BUILD" -j"$(sysctl -n hw.ncpu)"
	cmake -S "$ROOT/EFAndroid-SP/app/jni/macos" -B "$ENGINE_BUILD"
	cmake --build "$ENGINE_BUILD" -j"$(sysctl -n hw.ncpu)"
fi

# ---- 2. layout --------------------------------------------------------------
echo "Assembling $APP"
rm -rf "$APP"
MACOS="$APP/Contents/MacOS"
FW="$APP/Contents/Frameworks"
RES="$APP/Contents/Resources"
mkdir -p "$MACOS" "$FW" "$RES/vulkan/icd.d"

cp "$ENGINE_BUILD/efsp" "$MACOS/efsp"
# SP_ModulePath() (sp/sp_clink.c) loads these from the executable's directory.
cp "$GAME_BUILD/libefgame.dylib" "$GAME_BUILD/libefui.dylib" "$MACOS/"

# Runtime-loaded libraries (dlopen'd, so not visible to otool -L of efsp):
#   libSDL3      - loaded by sdl2-compat from @loader_path/libSDL3.dylib
#   libvulkan    - SDL_VULKAN_LIBRARY, set by Sys_MacSetupBundleEnv
#   libMoltenVK  - the Vulkan ICD, found through Resources/vulkan/icd.d
cp -L "$BREW/lib/libSDL3.0.dylib"      "$FW/libSDL3.dylib"
cp -L "$BREW/lib/libvulkan.1.dylib"    "$FW/libvulkan.1.dylib"
cp -L "$BREW/lib/libMoltenVK.dylib"    "$FW/libMoltenVK.dylib"
cp "$ROOT/macos/MoltenVK_icd.json" "$RES/vulkan/icd.d/MoltenVK_icd.json"
chmod u+w "$FW"/*

# ---- 3. copy + relink non-system dependencies -------------------------------
# deps_of FILE: install names of FILE's non-system dependencies (not itself).
deps_of() {
	otool -L "$1" | tail -n +2 | sed 's/^[[:space:]]*//; s/ (compatibility.*//' |
		grep -v -e '^/usr/lib/' -e '^/System/' -e '^@rpath/' -e '^@executable_path/' -e '^@loader_path/' |
		grep -v -x "$(otool -D "$1" | tail -n +2)" || true
}

relink() {   # relink FILE: copy its deps into Frameworks and point it at @rpath
	f="$1"
	for dep in $(deps_of "$f"); do
		base="$(basename "$dep")"
		if [ ! -e "$FW/$base" ]; then
			src="$dep"
			[ -e "$src" ] || { echo "missing dependency $dep (of $f)" >&2; exit 1; }
			cp -L "$src" "$FW/$base"
			chmod u+w "$FW/$base"
			QUEUE="$QUEUE $FW/$base"
		fi
		install_name_tool -change "$dep" "@rpath/$base" "$f" 2>/dev/null
	done
}

QUEUE="$MACOS/efsp $MACOS/libefgame.dylib $MACOS/libefui.dylib $FW/libSDL3.dylib $FW/libvulkan.1.dylib $FW/libMoltenVK.dylib"
DONE=""
while [ -n "$(echo $QUEUE)" ]; do
	set -- $QUEUE
	f="$1"; shift; QUEUE="$*"
	case " $DONE " in *" $f "*) continue ;; esac
	DONE="$DONE $f"
	relink "$f"
done

# Install names (ids) of the bundled libraries, and rpaths.
for lib in "$FW"/*.dylib; do
	install_name_tool -id "@rpath/$(basename "$lib")" "$lib" 2>/dev/null
done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$MACOS/efsp"
for lib in "$FW"/*.dylib; do
	# Frameworks libs find each other via their own @loader_path
	otool -l "$lib" | grep -q "path @loader_path " || install_name_tool -add_rpath "@loader_path" "$lib" 2>/dev/null
done

# ---- 4. Info.plist ------------------------------------------------------------
# LSMinimumSystemVersion = the newest minos of anything in the bundle
# (Homebrew bottles are built for the host macOS).
MIN_MACOS="$(for f in "$MACOS"/* "$FW"/*; do otool -l "$f" | awk '/minos/{print $2}'; done | sort -t. -k1,1n -k2,2n | tail -1)"
sed -e "s|@APP_NAME@|$DISPLAY_NAME|g" -e "s|@BUNDLE_ID@|$BUNDLE_ID|g" \
	-e "s|@VERSION@|$VERSION|g" -e "s|@BUILD@|$BUILD|g" -e "s|@MIN_MACOS@|$MIN_MACOS|g" \
	"$ROOT/macos/Info.plist.in" > "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" >/dev/null
printf 'APPL????' > "$APP/Contents/PkgInfo"

# ---- 5. verify + sign ---------------------------------------------------------
bad=0
for f in "$MACOS"/* "$FW"/*.dylib; do
	if otool -L "$f" | tail -n +2 | grep -v -e '/usr/lib/' -e '/System/' -e '@rpath/' -e '@loader_path/' -e '@executable_path/' >/dev/null; then
		echo "unbundled dependency in $f:" >&2
		otool -L "$f" | tail -n +2 | grep -v -e '/usr/lib/' -e '/System/' -e '@rpath/' >&2
		bad=1
	fi
done
[ "$bad" = 0 ] || exit 1

# Sign inside-out: libraries first, then the bundle (which signs the executable).
codesign --force -s - "$FW"/*.dylib "$MACOS"/*.dylib 2>&1 | grep -v "replacing existing signature" || true
codesign --force -s - --identifier "$BUNDLE_ID" "$APP" 2>&1 | grep -v "replacing existing signature" || true
codesign --verify --strict --deep "$APP"

echo "Built $APP (version $VERSION ($BUILD), macOS >= $MIN_MACOS, $(du -sh "$APP" | cut -f1))"
