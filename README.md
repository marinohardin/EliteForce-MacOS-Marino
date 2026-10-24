# VoyagerSP-MacOS
<img width="256" height="256" alt="VoyagerSP-MacOS Single Player Non-Starfleet App Icon" src="https://github.com/user-attachments/assets/28507cf3-8a2c-4235-b226-77997731af62" />

A native macOS (Apple Silicon) port of *Star Trek: Voyager — Elite Force*: the
**single-player campaign** (Raven Software, 2000).

It runs the original game's single-player campaign natively on arm64 Macs. The engine is an
idTech 3 derivative (lilium-voyager, an ioquake3 fork) with the Quake3e Vulkan renderer,
which runs on Metal through MoltenVK. Game logic comes from Raven's officially released
Elite Force SP source, built as native modules.

**No game data is included.** You need your own copy of the retail game. (Even the icon is intentionally not using copyrighted symbols.)

> Status: **beta.** The campaign is playable, my old PC savegames load, but it has not been played 
> through end to end on macOS yet. Please [report issues](#reporting-issues).

## Screenshots

<img width="1926" height="1232" alt="Elite Force Mac: Borg Approach" src="https://github.com/user-attachments/assets/11f67934-a3fe-44d7-b852-6be0d98eb6f4" />
<img width="1920" height="1235" alt="Elite Force Mac: Borg Lighting" src="https://github.com/user-attachments/assets/57a616ef-73a7-4cae-9b86-c3f2ad308839" />
<img width="1920" height="1110" alt="Elite Force Mac: Borg Holodeck" src="https://github.com/user-attachments/assets/432dfc63-aa8a-47c1-8b60-8f94f99087e2" />

## Features

- **Native arm64.** Built for Apple Silicon, with no Rosetta. The single-player game and UI
  modules are native `.dylib`s, not QVM bytecode.
- **Vulkan via MoltenVK.** The Quake3e `renderervk` renderer, running on Metal.
- **Retina / High-DPI.** With **High-DPI** on, the game renders at the display's native
  resolution (a 1920×1080 window renders at 3840×2160; fullscreen on a 5K panel at 5120×2880).
  The window stays the same size on screen either way, and it can be switched at any time
  from the Video menu, mid-mission too.
- **Native macOS fullscreen, switchable live.** The green window button, **Alt+Enter**,
  Ctrl+Cmd+F or Window → Enter Full Screen all toggle fullscreen at any time, including
  mid-mission; in fullscreen, move the pointer to the top edge for the menu bar. The window
  can also be resized by dragging. The in-game **Video** menu has **Window Size** (sizes that
  fit your screen), **Fullscreen** and **High-DPI**, and settings are remembered between
  launches. The default window is 1920×1080.
- **Bink cinematics** (intro, logos, in-game movies) decoded with FFmpeg.
- **Original PC saves load.** Savegames from the Windows version can be loaded.
- Mac keyboard and mouse: the macOS press-and-hold accent popup is disabled, so holding
  movement keys works, and text input is only active in the console, chat and menus.

## Requirements

- **An Apple Silicon Mac** (M1 or later). Intel Macs are not supported.
- **macOS 27 or later** (tested on an M1 Pro). The current builds bundle libraries built for
  macOS 27; support for older releases needs rebuilding those with an older deployment target.
- **The retail game.** Copy these files from the `BaseEF` folder of a PC installation:
  - `pak0.pk3` (about 541 MB, the main game data). This one is required.
  - The patch paks, such as `pak1.pk3` and `pak2.pk3` (and `pak3.pk3` if you have it).
    Recommended: they carry the 1.1/1.2 patch content.

  GOG and Steam copies work. A full install from the original CD works too. A
  **minimum install from the CD does not copy `pak0.pk3` to disk**: it stays on the CD, at
  `BaseEF/pak0.pk3`. Copy it from the CD yourself.

  The Expansion Pack is optional; see [Known issues](#known-issues).

## Install (players)

1. Download `VoyagerSP-MacOS.zip` from the
   [Releases page](https://github.com/marinohardin/VoyagerSP-MacOS/releases).
2. Unzip it and move **EliteForceSP.app** to `/Applications` (or anywhere you like).
3. Open the app. On first launch it looks for your game data, and if it can't find it, it
   asks you to **choose the folder where Elite Force is installed** (the folder that
   contains `BaseEF`):
   - By default the app **copies** the game files (`*.pk3`) and any saves from that
     `BaseEF` into its own data folder, so you can delete or move the original afterwards.
   - Tick **"Use BaseEF files in place rather than copy"** to read them from where they
     are instead (no copy, nothing extra on disk). The app remembers that folder.
   - If the chosen `BaseEF` has no `pak0.pk3` (a minimum CD install), the app says so;
     copy `pak0.pk3` from the CD and try again.

   It also quietly uses a `baseEF` folder placed next to the app, if there is one.
4. Release builds are not notarized, so the first time you open the app macOS may say it
   can't verify the developer. Either:
   - right-click (Control-click) the app, choose **Open**, then **Open** again, or
   - on newer macOS, go to **System Settings → Privacy & Security** and click
     **Open Anyway**, or
   - in Terminal: `xattr -dr com.apple.quarantine /Applications/EliteForceSP.app`

### Saves and settings

Config files, saves (and game files the app copied) live in
`~/Library/Application Support/EliteForce-MacOS/baseEF/`. You don't need to find that
folder yourself: the app's **File** menu has

- **Show Saves Folder** and **Show Game Folder**: open them in Finder,
- **Change Game Folder…**: pick a different install (takes effect on the next launch),
- **Delete Saves & Settings…**: moves the app's data folder to the Trash (it lists what's
  in it first). A game folder you chose to use in place is never touched.

Saves from the Windows version work: the app copies them when it copies your `BaseEF`, or
you can drop `.sav` files into the saves folder. If you played an earlier build of this
port, its saves (from `~/Library/Application Support/Lilium Voyager/` or
`VoyagerSP-MacOS/`) are carried over
automatically on first launch.

### Uninstall

Choose **File → Delete Saves & Settings…** (this also removes any game files the
app copied), then drag the app to the Trash.

## Building from source

Everything needed to build is in the repository. Game data is only needed to run.

### Prerequisites

- Xcode Command Line Tools: `xcode-select --install`
- [Homebrew](https://brew.sh) packages:

  ```sh
  brew install cmake pkg-config sdl2 molten-vk vulkan-loader
  ```

  (`sdl2` is provided by `sdl2-compat` on top of SDL3 in current Homebrew. That's fine.)
- FFmpeg for the cinematics: `macos/build-ffmpeg.sh` downloads FFmpeg 9.0.2 and builds a
  minimal LGPL version (Bink only) into `build-ffmpeg/install`. `make-app.sh` runs it when
  needed; the engine CMake uses it automatically, and falls back to a Homebrew `ffmpeg` if
  it hasn't been built.

### Build

The game modules and the engine are two separate CMake projects:

```sh
# 1. Single-player game + UI modules -> efgame/buildmac/libefgame.dylib, libefui.dylib
cmake -S efgame -B efgame/buildmac -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build efgame/buildmac -j

# 2. Engine executable -> build-macos/efsp  (run macos/build-ffmpeg.sh once first)
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

```sh
macos/make-app.sh             # builds both parts, then writes and ad-hoc signs dist/EliteForceSP.app
NO_BUILD=1 macos/make-app.sh  # re-bundle only
```

The script copies `efsp` and the two game modules into `Contents/MacOS`, and SDL3 +
sdl2-compat, the LGPL FFmpeg libraries, the Vulkan loader and MoltenVK into
`Contents/Frameworks` (license texts go in `Contents/Resources/licenses`), rewrites their install names to `@rpath`, and fails if anything still
points outside the bundle or the system. `APP_NAME`, `DISPLAY_NAME`, `BUNDLE_ID` and `BREW`
can be overridden.

Build options (engine CMake): `-DEF_BINK=OFF` builds without FFmpeg (no cinematics);
`-DEF_FFMPEG_PREFIX=/path` uses another FFmpeg install (empty: pkg-config's default).
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
  `/Applications/EliteForceSP.app/Contents/MacOS/efsp 2>&1 | tee ef.log`, or use
  `qconsole.log` (enable with `+set logfile 2`). Add `+set developer 1` for verbose logs.

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
