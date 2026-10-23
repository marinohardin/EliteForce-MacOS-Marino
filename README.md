# VoyagerSP-macOS

A native macOS (Apple Silicon) port of *Star Trek: Voyager — Elite Force*: the
**single-player campaign** (Raven Software, 2000).

It runs the original game's single-player campaign natively on arm64 Macs. The engine is an
idTech 3 derivative (lilium-voyager, an ioquake3 fork) with the Quake3e Vulkan renderer,
which runs on Metal through MoltenVK. Game logic comes from Raven's officially released
Elite Force SP source, built as native modules.

**No game data is included.** You need your own copy of the retail game.

> Status: **alpha.** The campaign is playable, but it has not been played through end to
> end on macOS yet. Please [report issues](#reporting-issues).

## Screenshots

<!-- TODO(screenshots): add 2-3 screenshots (main menu, first mission, Retina fullscreen).
     Use your own captures. Don't use official promo art or Star Trek logos. -->

*Screenshots coming soon.*

## Features

- **Native arm64.** Built for Apple Silicon, with no Rosetta. The single-player game and UI
  modules are native `.dylib`s, not QVM bytecode.
- **Vulkan via MoltenVK.** The Quake3e `renderervk` renderer, running on Metal.
- **Retina / High-DPI.** `r_highdpi 1` renders at the display's native backing resolution
  (for example 5120×2880 fullscreen on a 5K panel), with menu mouse coordinates mapped
  correctly.
- **Windowed or fullscreen.** `r_fullscreen`, `r_mode` / `r_customwidth` /
  `r_customheight` work on desktop.
  <!-- TODO(video-menu): mention the in-game Video menu once the video-settings branch is merged. -->
- **Bink cinematics** (intro, logos, in-game movies) decoded with FFmpeg.
- **Original PC saves load.** Savegames from the Windows version can be loaded.
- Mac keyboard and mouse: the macOS press-and-hold accent popup is disabled, so holding
  movement keys works, and text input is only active in the console, chat and menus.

## Requirements

- **An Apple Silicon Mac** (M1 or later). Intel Macs are not supported.
- **macOS:** tested on **macOS 27 on an M1 Pro**. Older releases that MoltenVK supports
  may work but are untested.
  <!-- TODO(min-macos): set to the LSMinimumSystemVersion that macos/make-app.sh writes. -->
- **The retail game.** Copy these files from the `BaseEF` folder of a PC installation:
  - `pak0.pk3` (about 541 MB, the main game data). This one is required.
  - The patch paks, such as `pak1.pk3` and `pak2.pk3` (and `pak3.pk3` if you have it).
    Recommended: they carry the 1.1/1.2 patch content.

  GOG and Steam copies work. A full install from the original CD works too. A
  **minimum install from the CD does not copy `pak0.pk3` to disk**: it stays on the CD, at
  `BaseEF/pak0.pk3`. Copy it from the CD yourself.

  The Expansion Pack is optional; see [Known issues](#known-issues).

## Install (players)

1. Download `VoyagerSP-macOS.zip` from the
   [Releases page](https://github.com/OWNER/VoyagerSP-macOS/releases). <!-- TODO(release-link): real owner/URL -->
2. Unzip it and move **VoyagerSP-macOS.app** to `/Applications` (or anywhere you like).
3. Put your game data where the app looks for it.
   <!-- TODO(data-location): the app-bundle branch decides where BaseEF goes (likely
        ~/Library/Application Support/<name>/baseEF/). Replace this step with the exact
        path and what the in-app "missing data" message says. -->
   For now, see the message the app shows on first launch when it can't find
   `pak0.pk3`. It tells you where to put your `BaseEF` folder.
4. Open the app. Release builds are not notarized, so the first time macOS says it
   can't verify the developer. Either:
   - right-click (Control-click) the app, choose **Open**, then **Open** again, or
   - on newer macOS, go to **System Settings → Privacy & Security** and click
     **Open Anyway**, or
   - in Terminal: `xattr -dr com.apple.quarantine /Applications/VoyagerSP-macOS.app`

### Saves and settings

Config files and saves live in `~/Library/Application Support/Lilium Voyager/baseEF/`
(the engine's home path). To keep playing a Windows save, copy the files from the
Windows install's `BaseEF/save/` folder into the `save/` folder there.
<!-- TODO(verify-homepath): confirm the path if the app-bundle branch changes HOMEPATH_NAME. -->

## Building from source

Everything needed to build is in the repository. Game data is only needed to run.

### Prerequisites

- Xcode Command Line Tools: `xcode-select --install`
- [Homebrew](https://brew.sh) packages:

  ```sh
  brew install cmake pkg-config sdl2 ffmpeg molten-vk vulkan-loader
  ```

  (`sdl2` is provided by `sdl2-compat` on top of SDL3 in current Homebrew. That's fine.)

### Build

The game modules and the engine are two separate CMake projects:

```sh
# 1. Single-player game + UI modules -> efgame/buildmac/libefgame.dylib, libefui.dylib
cmake -S efgame -B efgame/buildmac -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build efgame/buildmac -j

# 2. Engine executable -> build-macos/efsp
cmake -S EFAndroid-SP/app/jni/macos -B build-macos
cmake --build build-macos -j
```

### Run from the build tree

`run-macos.sh` copies the dylibs next to the executable, points SDL at Homebrew's Vulkan
loader, and starts a 1280×720 window:

```sh
./run-macos.sh                                   # data in ./Star Trek Voyager Elite Force/BaseEF
EF_DIR=/path/to/EliteForce ./run-macos.sh        # a folder that contains BaseEF/
EF_DIR=/path/to/EliteForce ./run-macos.sh +set r_fullscreen 1 +set r_highdpi 1
```

`EF_DIR` is the folder that *contains* `BaseEF` (the `fs_basepath`), not `BaseEF` itself.

### Make the .app

<!-- TODO(verify-make-app): check the usage and output path once the app-bundle branch is merged. -->
```sh
macos/make-app.sh        # bundles efsp, the dylibs, SDL, FFmpeg, the Vulkan loader and MoltenVK
```

Build options (engine CMake): `-DEF_BINK=OFF` builds without FFmpeg (no cinematics).
`-DEF_VM_JIT=ON` enables the experimental aarch64 QVM JIT; it is off by default and not
needed, because the SP game runs natively.

Technical background on the port is in
[docs/macos-port-notes.md](docs/macos-port-notes.md).

## Known issues

- **Virtual Voyager (Expansion Pack) is not supported** without the Expansion Pack's data.
  If its maps are missing, the main-menu button shows **EXPANSION ONLY** and is greyed out.
- **Alpha quality.** Expect occasional rendering, audio or animation glitches inherited from
  the Android port. Back up your saves.
- Apple Silicon only, and the app is not notarized (see Install step 4).

## Reporting issues

Please open an issue on GitHub with:

- your Mac model and macOS version,
- where your game data comes from (GOG, Steam, CD + patch) and which `pak*.pk3` files you have,
- what happened and how to reproduce it (map name, save file if relevant),
- the console log. Start from Terminal with
  `/Applications/VoyagerSP-macOS.app/Contents/MacOS/<executable> 2>&1 | tee ef.log`, or use
  `qconsole.log` (enable with `+set logfile 2`).
  <!-- TODO(verify-make-app): fill in the bundle executable name. -->

See also [CONTRIBUTING.md](CONTRIBUTING.md).

## Origins

This is a fork of [VoyagerSP-Android](https://github.com/imjustadudegamer/VoyagerSP-Android)
by imjustadudegamer, the Android port of Elite Force single-player. That port was inspired by
[VoyagerNX](https://github.com/faithvoid/VoyagerNX), faithvoid's Nintendo Switch port. It is
built on [lilium-voyager](https://github.com/clover-moe/lilium-voyager) (Zack Middleton),
[ioquake3](https://github.com/ioquake/ioq3) and id Software's Quake III Arena, with the
Vulkan renderer from [Quake3e](https://github.com/ec-/Quake3e) (Eugene C.). The Android
sources (`EFAndroid-SP/app/src`, Gradle files) are still in the tree, untouched. The macOS
build only uses the shared engine and game code. Full attribution:
[CREDITS.md](CREDITS.md).

## License

This repository contains code under **two different licenses**, plus a trademark you should
know about:

- **Engine and port code: GNU GPL version 2 or later** ([LICENSE](LICENSE)). This covers
  everything under `EFAndroid-SP/` (the idTech 3 engine from id Software → ioquake3 →
  lilium-voyager, the Quake3e Vulkan renderer) and the macOS port changes. You may use,
  modify and redistribute it under the GPL, including providing source with any binaries.
- **Single-player game module (`efgame/`): Raven Software's "Limited Use Software License
  Agreement"** (`efgame/STEF Game Source License.doc`). **Not GPL, not open source.** In plain
  words, it allows you to modify the code and distribute it **free of charge, for
  non-commercial purposes only**, with a copy of that license, and **only for use with the
  full retail version of Elite Force**. Selling it or using it commercially in any way is not
  allowed.
- **No game assets are included.** Maps, models, textures, sounds, music and videos belong to
  their owners and come from your own retail copy.
- **Star Trek** and related marks are trademarks of **Paramount** (CBS Studios). *Elite Force*
  was developed by Raven Software and published by Activision. This is an unofficial,
  non-commercial fan project. It is not affiliated with or endorsed by any of them.

Release builds also bundle third-party libraries (SDL, FFmpeg, the Vulkan loader, MoltenVK)
under their own licenses. See [THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md).
