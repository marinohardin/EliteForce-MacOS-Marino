# Building AwayTeamEF from source

This covers the Mac app. The Windows and Linux builds are described in [windows/ReadMe.txt](../windows/ReadMe.txt), [linux/ReadMe.txt](../linux/ReadMe.txt) and the CI workflow in `.github/workflows/build.yml`.


Everything needed to build is in the repository. Game data is only needed to run.

## Prerequisites

- Xcode Command Line Tools: `xcode-select --install`
- [Homebrew](https://brew.sh) packages:

  ```sh
  brew install cmake ninja pkg-config molten-vk vulkan-headers vulkan-loader
  ```

  (`vulkan-loader` is only used by `run-macos.sh`; the `.app` bundles its own.)
- SDL and the Vulkan loader for the app: `macos/build-deps.sh` builds SDL3 3.4.18,
  sdl2-compat 2.32.74 and the Vulkan loader 1.4.363 (the releases Homebrew ships, checksums
  pinned) for the app's minimum macOS into `build-deps/install`. Homebrew's own builds target
  the build Mac's macOS, so bundling them would raise the app's minimum. `make-app.sh` runs it
  when needed; the engine CMake uses it automatically (else pkg-config's `sdl2`).
- FFmpeg for the cinematics: `macos/build-ffmpeg.sh` downloads FFmpeg 9.0.2 and builds a
  minimal LGPL version (Bink only, for the app's minimum macOS) into `build-ffmpeg/install`. `make-app.sh` runs it when
  needed; the engine CMake uses it automatically, and falls back to a Homebrew `ffmpeg` if
  it hasn't been built.

## Build

The game modules and the engine are two separate CMake projects:

```sh
# 1. Single-player game + UI modules -> efgame/buildmac/libefgame.dylib, libefui.dylib
cmake -S efgame -B efgame/buildmac -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build efgame/buildmac -j

# 2. Engine executable -> build-macos/efsp  (run macos/build-ffmpeg.sh once first)
cmake -S EFAndroid-SP/app/jni/macos -B build-macos
cmake --build build-macos -j
```

## Run from the build tree

`run-macos.sh` copies the dylibs next to the executable, points SDL at Homebrew's Vulkan
loader, and starts a 1280×720 window:

```sh
./run-macos.sh                                   # data in ./Star Trek Voyager Elite Force/BaseEF
EF_DIR=/path/to/EliteForce ./run-macos.sh        # a folder that contains BaseEF/
EF_DIR=/path/to/EliteForce ./run-macos.sh +set r_fullscreen 1 +set r_highdpi 1
```

`EF_DIR` is the folder that *contains* `BaseEF` (the `fs_basepath`), not `BaseEF` itself.

## Make the .app

```sh
macos/make-app.sh             # builds both parts, then writes and ad-hoc signs "dist/Elite Force.app"
NO_BUILD=1 macos/make-app.sh  # re-bundle only
```

`macos/make-release.sh` builds the app and packages it for a release as a `.dmg` (with an
Applications shortcut) and a `.zip` in `dist/`.

Local builds are ad-hoc signed and run on your own Mac without an Apple developer account.
With a Developer ID certificate, `make-release.sh` also signs, notarizes and staples the
release ([docs/PUBLISHING.md](PUBLISHING.md)).

The script copies `efsp` and the two game modules into `Contents/MacOS`, and SDL3 +
sdl2-compat, the LGPL FFmpeg libraries, the Vulkan loader and MoltenVK into
`Contents/Frameworks` (license texts go in `Contents/Resources/licenses`), rewrites their install names to `@rpath`, and fails if anything still
points outside the bundle or the system. cMod's game modules go in `Contents/Resources/cmod`
from a local copy of the pk3 (pk3s aren't in git): the newest `macos/cmod/pakcmod-release-*.pk3`,
or `CMOD_PAK=<file>`; see `macos/cmod/README.txt`. Without one the app is built without them. `APP_NAME`, `DISPLAY_NAME`, `BUNDLE_ID` and `BREW`
can be overridden.

Build options (engine CMake): `-DEF_BINK=OFF` builds without FFmpeg (no cinematics);
`-DEF_FFMPEG_PREFIX=/path` uses another FFmpeg install (empty: pkg-config's default).
`-DEF_VM_JIT=ON` enables the experimental aarch64 QVM JIT; it is off by default and not
needed: the SP game runs natively, and the interpreter is fast enough for Holomatch.

The engine runs single player by default; `+set com_holomatch 1` starts Holomatch (the
launcher passes it; it is command-line only). From the `.app`, naming the mode on the
command line also skips the launcher:
`"Elite Force.app/Contents/MacOS/Elite Force" +set com_holomatch 1`.

Technical background on the port is in
[docs/macos-port-notes.md](macos-port-notes.md).

