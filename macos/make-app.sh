#!/bin/sh
# Build the macOS engine (single player + Holomatch) + SP game modules and assemble a self-contained,
# ad-hoc signed app bundle:  "dist/Elite Force.app"
#
#   macos/make-app.sh               build (incremental) + bundle
#   NO_BUILD=1 macos/make-app.sh    bundle the existing build outputs only
#
# Needs Xcode command line tools, cmake, ninja, pkg-config and the Homebrew
# packages molten-vk and vulkan-headers. SDL3, sdl2-compat and the Vulkan loader
# (macos/build-deps.sh) and FFmpeg (macos/build-ffmpeg.sh) are built from source
# for the app's minimum macOS (MIN_MACOS_TARGET, default 12.0), on demand.
# FFmpeg is the minimal LGPL build from macos/build-ffmpeg.sh (run on demand).
# Every non-system dylib (and its transitive deps) is copied into
# Contents/Frameworks and relinked to @rpath, so the .app runs without Homebrew.
# The game data is NOT bundled (see Sys_MacCheckGameData in efcode/sys/sys_osx.m).
set -eu

APP_NAME="${APP_NAME:-Elite Force}"
DISPLAY_NAME="${DISPLAY_NAME:-Elite Force}"
# the executable's name is what macOS shows in some prompts (Local Network)
# and in Activity Monitor; the build calls it efsp
EXE_NAME="${EXE_NAME:-$DISPLAY_NAME}"
BUNDLE_ID="${BUNDLE_ID:-org.voyagersp.macos}"
BREW="${BREW:-$(brew --prefix 2>/dev/null || echo /opt/homebrew)}"

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ENGINE_BUILD="$ROOT/build-macos"
GAME_BUILD="$ROOT/efgame/buildmac"
DIST="$ROOT/dist"
# assembled under a name without spaces (the relink queue below splits on
# them), then renamed to "$APP_NAME.app"
FINAL="$DIST/$APP_NAME.app"
APP="$DIST/.assembling-$$.app"
# the macOS port's own version (not the Android app's versionName)
VERSION="${VERSION:-1.2.3}"
BUILD="$(git -C "$ROOT" rev-list --count HEAD 2>/dev/null || echo 1)"

# ---- 1. build ---------------------------------------------------------------
FFMPEG="$ROOT/build-ffmpeg/install"
# the oldest macOS the app supports: the engine, the game modules and the libraries
# from build-deps.sh / build-ffmpeg.sh are built for it (MoltenVK from Homebrew
# already supports it)
MIN_MACOS_TARGET="${MIN_MACOS_TARGET:-12.0}"
DEPS="$ROOT/build-deps/install"
if [ -z "${NO_BUILD:-}" ]; then
	[ -f "$FFMPEG/lib/pkgconfig/libavcodec.pc" ] && [ "$(otool -l "$FFMPEG/lib/libavutil.dylib" | awk '/minos/{print $2; exit}')" = "$MIN_MACOS_TARGET" ] ||
		MIN_MACOS="$MIN_MACOS_TARGET" "$ROOT/macos/build-ffmpeg.sh"
	[ -f "$DEPS/lib/pkgconfig/sdl2.pc" ] && [ "$(otool -l "$DEPS/lib/libSDL3.0.dylib" | awk '/minos/{print $2; exit}')" = "$MIN_MACOS_TARGET" ] ||
		MIN_MACOS="$MIN_MACOS_TARGET" "$ROOT/macos/build-deps.sh"
	cmake -S "$ROOT/efgame" -B "$GAME_BUILD" -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS_TARGET"
	cmake --build "$GAME_BUILD" -j"$(sysctl -n hw.ncpu)"
	cmake -S "$ROOT/EFAndroid-SP/app/jni/macos" -B "$ENGINE_BUILD" -DEF_FFMPEG_PREFIX="$FFMPEG" \
		-DEF_DEPS_PREFIX="$DEPS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MIN_MACOS_TARGET" -DEF_APP_VERSION="$VERSION"
	cmake --build "$ENGINE_BUILD" -j"$(sysctl -n hw.ncpu)"
fi

# ---- 2. layout --------------------------------------------------------------
echo "Assembling $FINAL"
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
#   libSDL2      - sdl2-compat, linked by efsp as @rpath/libSDL2-2.0.0.dylib
# SDL and the Vulkan loader come from build-deps.sh (built for MIN_MACOS_TARGET)
cp -L "$DEPS/lib/libSDL3.0.dylib"         "$FW/libSDL3.dylib"
cp -L "$DEPS/lib/libSDL2-2.0.0.dylib"     "$FW/libSDL2-2.0.0.dylib"
cp -L "$DEPS/lib/libvulkan.1.dylib"       "$FW/libvulkan.1.dylib"
cp -L "$BREW/lib/libMoltenVK.dylib"       "$FW/libMoltenVK.dylib"
# The LGPL FFmpeg's install names are already @rpath/..., so the relink pass
# below skips them: copy them explicitly (whatever efsp links, nothing more).
for dep in $(otool -L "$MACOS/efsp" | awk '/@rpath\/lib(av|sw)/{print $1}'); do
	base="${dep#@rpath/}"
	[ -e "$FFMPEG/lib/$base" ] || { echo "efsp links $dep but $FFMPEG/lib has no $base" >&2; exit 1; }
	cp -L "$FFMPEG/lib/$base" "$FW/$base"
done
# LGPL: FFmpeg's license text, plus where its exact source and build options are
FF_VERSION="$(sed -n 's/^FFMPEG_VERSION="\(.*\)"/\1/p' "$ROOT/macos/build-ffmpeg.sh")"
mkdir -p "$RES/licenses"
cp "$ROOT/build-ffmpeg/ffmpeg-$FF_VERSION/COPYING.LGPLv2.1" "$RES/licenses/FFmpeg-LGPL-2.1.txt"
cat > "$RES/licenses/FFmpeg-README.txt" <<EOF_FF
The libav*/libsw* libraries in Contents/Frameworks are FFmpeg $FF_VERSION
(https://ffmpeg.org), licensed under the GNU LGPL 2.1 or later (FFmpeg-LGPL-2.1.txt).
They are unmodified and built from https://ffmpeg.org/releases/ffmpeg-$FF_VERSION.tar.xz
by macos/build-ffmpeg.sh in the project's source repository, which holds the exact
configure options. You may replace them with your own build of the same version.
EOF_FF
cp "$ROOT/THIRD_PARTY_LICENSES.md" "$ROOT/LICENSE" "$RES/licenses/"
# Raven's license for the game modules must ship with them
cp "$ROOT/efgame/STEF Game Source License.doc" "$RES/licenses/"
cp "$ROOT/macos/MoltenVK_icd.json" "$RES/vulkan/icd.d/MoltenVK_icd.json"
cp "$ROOT/macos/Credits.html" "$RES/Credits.html"	# the About panel shows it
"$ROOT/macos/icon/make-icns.sh" "$RES/AppIcon.icns"
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

QUEUE="$MACOS/efsp $MACOS/libefgame.dylib $MACOS/libefui.dylib $FW/libSDL3.dylib $FW/libSDL2-2.0.0.dylib $FW/libvulkan.1.dylib $FW/libMoltenVK.dylib"
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
# drop build-tree rpaths (e.g. build-ffmpeg/install/lib) CMake left in efsp
for rp in $(otool -l "$MACOS/efsp" | awk '/cmd LC_RPATH/{getline; getline; print $2}'); do
	case "$rp" in /*) install_name_tool -delete_rpath "$rp" "$MACOS/efsp" ;; esac
done
install_name_tool -add_rpath "@executable_path/../Frameworks" "$MACOS/efsp"
for lib in "$FW"/*.dylib; do
	# Frameworks libs find each other via their own @loader_path
	otool -l "$lib" | grep -q "path @loader_path " || install_name_tool -add_rpath "@loader_path" "$lib" 2>/dev/null
done

# the bundle's executable; renamed only now, the steps above split paths on spaces
mv "$MACOS/efsp" "$MACOS/$EXE_NAME"

# ---- 4. Info.plist ------------------------------------------------------------
# LSMinimumSystemVersion = the newest minos of anything in the bundle
# (Homebrew bottles are built for the host macOS).
MIN_MACOS="$(for f in "$MACOS"/* "$FW"/*; do otool -l "$f" | awk '/minos/{print $2}'; done | sort -t. -k1,1n -k2,2n | tail -1)"
sed -e "s|@APP_NAME@|$DISPLAY_NAME|g" -e "s|@EXECUTABLE@|$EXE_NAME|g" -e "s|@BUNDLE_ID@|$BUNDLE_ID|g" \
	-e "s|@VERSION@|$VERSION|g" -e "s|@BUILD@|$BUILD|g" -e "s|@MIN_MACOS@|$MIN_MACOS|g" \
	"$ROOT/macos/Info.plist.in" > "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" >/dev/null
printf 'APPL????' > "$APP/Contents/PkgInfo"

touch "$APP"	# so Finder/Dock pick up a changed icon

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

rm -rf "$FINAL"
mv "$APP" "$FINAL"
APP="$FINAL"
echo "Built $APP (version $VERSION ($BUILD), macOS >= $MIN_MACOS, $(du -sh "$APP" | cut -f1))"
