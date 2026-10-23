# Credits & Acknowledgements

**This macOS port is a fork of [VoyagerSP-Android](https://github.com/imjustadudegamer/VoyagerSP-Android)
by imjustadudegamer**, the native Android port of the Elite Force single-player campaign, which in turn
was inspired by and built on faithvoid's VoyagerNX. Nearly everything that makes the single-player game
run on a modern idTech 3 engine (the SP game/engine bridge, the Vulkan renderer port to the Elite Force
engine, the savegame work, cutscene and audio fixes) comes from that project. The macOS-specific
additions are listed in [macOS port](#macos-port) at the bottom.

Every component below is gratefully credited to its authors. Nothing here is claimed as original except
the port work explicitly listed under "This port" (Android) and "macOS port".

## Direct upstream

- **VoyagerSP-Android** — imjustadudegamer — the Android single-player port this repository is forked
  from. <https://github.com/imjustadudegamer/VoyagerSP-Android>

## The game

- **Star Trek: Voyager — Elite Force** — developed by **Raven Software**, published by **Activision**,
  under license from **Paramount**. All Elite Force trademarks, characters, and assets are the property
  of their respective owners. No retail assets are distributed in this repository.

## Single-player game module (`efgame/`)

- Based on the **official Elite Force Single-Player mod source SDK**, released by **Raven Software** for
  community modding. Public mirror: <https://github.com/UberGames/SP-Mod-Source-Code>.
- Licensed under the **STEF Game Source License** (Raven/Activision) — see that license; it is **not**
  GPL. The game module retains this license; only the Android port modifications are added on top.
- Thanks to the **"The Dark Project" / Voyager: Insurrection** mod team, whose advocacy led Raven to
  release the SP source SDK.

## Engine (`EFAndroid-SP/app/jni/efcode`)

idTech3 lineage, all **GPLv2**:

- **Quake III Arena** — id Software. <https://github.com/id-Software/Quake-III-Arena>
- **ioquake3** — the ioquake3 contributors. <https://github.com/ioquake/ioq3>
- **lilium-voyager** — Zack Middleton — the ioquake3-based Elite Force engine port that this engine
  half builds on. <https://github.com/clover-moe/lilium-voyager>
- **Quake3e** — Eugene C. (ec-) — source of the Vulkan renderer (`renderervk`) and the AArch64/ARMv7
  VM JIT (`vm_aarch64.c`, `vm_armv7l.c`). <https://github.com/ec-/Quake3e>
- **VoyagerNX** — faithvoid — the Nintendo Switch port that proved the ARM + console-controls path this
  Android lineage started from. <https://github.com/faithvoid/VoyagerNX>

## Bundled libraries

- **SDL2** — Sam Lantinga and the SDL contributors (zlib license). <https://libsdl.org>
- **FFmpeg** — the FFmpeg team (LGPL/GPL) — used for Bink/RoQ cutscene video decoding.
  <https://ffmpeg.org>
- **libmad** 0.15.1b — Underbit Technologies (GPLv2) — MP3 audio decoding.
- **zlib** — Jean-loup Gailly and Mark Adler (zlib license).
- **libjpeg** (jpeg-8c) — the Independent JPEG Group.
- **LCC** — Christopher Fraser and David Hanson — the retargetable C compiler used to build QVM bytecode
  (`efcode/tools/lcc`).

## References consulted (not bundled as code)

- **OpenJK** — the JACoders team — reference for 64-bit idTech3 savegame/ICARUS behavior during the
  arm64 LP64 port. <https://github.com/JACoders/OpenJK>

## Not included (proprietary — supply your own)

- **Bink Video** — RAD Game Tools / Epic (`binkw32.dll`, `binkw32` runtime) — not redistributed.
- **Retail Elite Force game data** (`*.pk3`, retail `*.exe`/`*.dll`) — not redistributed; you must
  provide your own legally-owned copies.

## This port (Android, upstream)

- The Android single-player port and handheld integration — JNI engine bridge, Vulkan renderer fixes,
  touch and gamepad controls, 4:3/16:9 aspect handling, save-menu thumbnails, the ILP32↔LP64
  (armv7/arm64) savegame transcoder, and assorted bridge/cutscene/audio fixes — were contributed by the
  VoyagerSP-Android authors and contributors (imjustadudegamer et al.).

## macOS port

This fork adds a native macOS (Apple Silicon) build. Technical log: [docs/macos-port-notes.md](docs/macos-port-notes.md).

- **Build system.** A separate CMake build for the engine executable
  (`EFAndroid-SP/app/jni/macos/CMakeLists.txt`) using lilium-voyager's macOS sys layer (`sys_osx.m`,
  `con_tty.c`) and Homebrew SDL2/FFmpeg. `efgame/CMakeLists.txt` builds `libefgame.dylib` and `libefui.dylib`
  with ld64 exported-symbols lists. An `android/log.h` stderr shim. `run-macos.sh` launcher. Release
  (NDEBUG) build.
- **dylib game modules.** Raven's 2000-era Mac OS X Server block in `q_shared.h` forced
  GAME/CGAME/UI_HARD_LINKED on any `__APPLE__` build, so the modules could not load. It is now guarded
  by `EF_DYLIB_MODULES`. Module paths resolve to `<exe dir>/*.dylib`.
- **Graphics and windowing.** Vulkan through MoltenVK, loaded at runtime via SDL (sdl2-compat on SDL3).
  Desktop builds honour `r_fullscreen` and `r_customwidth`/`r_customheight`. `r_highdpi` gives native
  Retina rendering, and menu mouse coordinates are mapped from points to pixels.
- **Apple arm64 ABI fix.** The cgame's `vmMain` is now called through its real fixed-argument type. On
  Apple arm64, variadic arguments go on the stack (unlike Linux/Android AAPCS64), which broke
  `CG_DRAW_ACTIVE_FRAME` ("Undefined stereoView").
- **items.dat hole fix.** `bg_itemlist` holes with no retail `items.dat` entry (`ITM_CHAOS_PICKUP`)
  get a placeholder classname, so item lookups no longer stop early ("Couldn't find item for weapon
  weapon_melee"). Not macOS-specific.
- **Input.** The press-and-hold accent picker is disabled, and SDL text input is only active while the
  console, chat or a menu has key focus, so holding WASD works.
- **UI.** The Virtual Voyager button shows "EXPANSION ONLY" (greyed out) when the Expansion Pack maps
  are missing, instead of "?" and a load error.
- **Self-contained .app.** `macos/make-app.sh` bundles the engine, game modules, SDL, FFmpeg, the Vulkan
  loader and MoltenVK with `@rpath` install names and ad-hoc signing; the app finds the bundled
  MoltenVK ICD itself. First launch finds or asks for the game folder (copy or use in place), and the
  app menu has Show Saves/Game Folder, Change Game Folder and Delete Saves & Settings.
- **Live video settings.** Alt+Enter and the Video menu switch fullscreen and window size without a
  relaunch (in-place swapchain resize, window-recreating `vid_restart` when needed), plus a High-DPI
  option; the SP cgame follows the new size mid-mission.
- **Log cleanup.** Bridge logs go through the engine console (no more mangled `tty]` lines), stale UI
  shader handles are refreshed after a renderer restart (no `hShader out of range` spam, no stray
  box on the galaxy map), and cgame commands are registered once.

### Bundled in the macOS .app

The macOS release bundles these libraries as shared libraries. See
[THIRD_PARTY_LICENSES.md](THIRD_PARTY_LICENSES.md) for details and license texts.

- **MoltenVK** — The Brenwill Workshop / Khronos Group (Apache-2.0) — Vulkan on Metal.
  <https://github.com/KhronosGroup/MoltenVK>
- **Vulkan-Loader** — The Khronos Group, LunarG, Valve (Apache-2.0).
  <https://github.com/KhronosGroup/Vulkan-Loader>
- **SDL** — sdl2-compat and SDL3 (zlib license). <https://libsdl.org>
- **FFmpeg** — the FFmpeg developers. The bundled build comes from Homebrew, which configures FFmpeg
  with `--enable-gpl --enable-version3`, so **the bundled FFmpeg is licensed under the GPL version 3 or
  later** (not LGPL). It also pulls in its own GPL dependencies (x264, x265). Source:
  <https://ffmpeg.org/download.html>.

---

If any attribution here is incomplete or incorrect, please open an issue — it will be fixed promptly.
