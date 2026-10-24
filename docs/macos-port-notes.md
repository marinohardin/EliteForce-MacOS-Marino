# macOS port notes: original feasibility and porting log

> **Historical document.** This is the original feasibility spike report written on
> 2026-10-23, when the macOS port was started, kept as a record of what was changed and why.
> It is not kept up to date. For current build and install instructions see the
> [README](../README.md); for attribution see [CREDITS.md](../CREDITS.md). Paths such as
> `~/gitdir/VoyagerSP-macos` refer to the author's machine at the time. Later work (Retina
> rendering, input fixes, the Expansion-only menu entry, the `.app` bundle) is in the git
> history (`git log 0d8942e..`).

---

## Original report header

Repo: `~/gitdir/VoyagerSP-macos`, a full clone of
https://github.com/imjustadudegamer/VoyagerSP-Android. Work is on the local branch `macos-port`,
on top of upstream `0d8942e` (release 1.2.0). Nothing is pushed.

Machine: Apple M1 Pro, macOS 27, Xcode clang, Homebrew cmake 4 / sdl2-compat 2.32 /
ffmpeg 9.0.2. molten-vk, vulkan-loader and vulkan-headers 1.4.363 were installed for this spike.

## Verdict: feasible. It builds and boots natively with modest changes

| Piece | Result |
|---|---|
| `efgame` → `libefgame.dylib` (game+cgame+icarus) | builds, exports only GetGameAPI/vmMain/dllEntry, `dlopen(RTLD_NOW)` OK |
| `efgame` → `libefui.dylib` | builds, exports only GetUIAPI, loads OK |
| Engine → `efsp` executable (Vulkan-only renderervk) | builds and links. Bink via Homebrew FFmpeg 9 compiles unchanged |
| Launch, no data | prints `"pak0.pk3" is missing…` then shows the error dialog (Sys_Error → Cocoa alert) |
| Launch with a 1-file test fixture (`com_basegame spiketest`, contains only `default.cfg`) | full R_Init via MoltenVK (Apple M1 Pro, swapchain B8G8R8A8, IMMEDIATE present). Window opened, CoreAudio up, `libefui.dylib` loaded from the exe dir, UI_Init ran until it hit the missing retail `SP_BUTTONTEXT.DAT` |

Build and run commands:
```sh
cmake -S efgame -B efgame/buildmac -DCMAKE_OSX_ARCHITECTURES=arm64 && cmake --build efgame/buildmac -j10
cmake -S EFAndroid-SP/app/jni/macos -B build-macos && cmake --build build-macos -j10
cp efgame/buildmac/*.dylib build-macos/
SDL_VULKAN_LIBRARY=/opt/homebrew/lib/libvulkan.1.dylib build-macos/efsp +set fs_basepath <dir containing baseEF>
```

## Every change, and why (3 commits on `macos-port`)
1. **efgame link flags** (`efgame/CMakeLists.txt`, new `android/efgame.exp`, `efui.exp`).
   ld64 has no `--version-script`, `--unresolved-symbols` or `-z max-page-size`. Under
   `if(APPLE)` these become `-exported_symbols_list` (Mach-O names with an extra leading `_`)
   and `-undefined dynamic_lookup`. The Android branch is untouched.
2. **`android/log.h` shim** (`EFAndroid-SP/app/jni/macos/android/log.h`). The four `sp/` files
   include it unconditionally. It sends `__android_log_print` / `__android_log_vprint` to stderr.
3. **`q_shared.h` OS X hard-link block** (`efgame/src/game/q_shared.h`, `-DEF_DYLIB_MODULES`).
   Raven's 2000-era "Mac OS X Server" section defines GAME/CGAME/UI_HARD_LINKED on any
   `__APPLE__` build. That compiles out the modules' own `Com_Printf`, `Com_Error` and
   `Field_*`, so dlopen failed with `symbol not found in flat namespace '__Z10Com_PrintfPKcz'`.
   The block is now guarded by `#ifndef EF_DYLIB_MODULES`. (This was the only real surprise.)
4. **Module paths** (`sp/sp_clink.c`, `sp_bridge.cpp`, `sp_ui_bridge.cpp`). The new
   `SP_ModulePath()` maps `libefgame.so` → `<Sys_BinaryPath()>/libefgame.dylib` on Apple and
   returns the name unchanged elsewhere. Android relies on the APK lib dir to resolve bare sonames.
5. **`IN_ClearCrouchToggle`** (`efcode/sdl/sdl_input.c`). It is defined only inside the
   `__ANDROID__` touch block, but `sp_integration.c` always calls it. Added a no-op under `#ifndef __ANDROID__`.
6. **macOS engine CMake** (`EFAndroid-SP/app/jni/macos/CMakeLists.txt`, new, separate from the
   Android one). It uses the same source lists, but:
   - builds an executable instead of libmain.so, with no `main=SDL_main` rename;
   - uses lilium's macOS sys layer (`con_tty.c`, `sys_osx.m`) in place of `con_passive.c`;
   - links Homebrew SDL2 and FFmpeg through pkg-config, plus Cocoa and IOKit. No GLES, EGL or log libraries;
   - leaves the QVM JIT off (interpreter), behind the `EF_VM_JIT` option;
   - passes `-Wno-error=incompatible-function-pointer-types`, because Apple clang 17 errors on
     `ri.Cvar_VariableString = Cvar_VariableString` (char* vs const char*), which NDK clang
     only warned about;
   - has `EF_BINK=OFF`, which swaps in `macos/bink_stub.c`. It isn't needed, because `bink_ff.c`
     builds against FFmpeg 9 as-is, but cinematic playback is untested without data.

## What blocked (all resolved)
- The Claude Code auto-mode classifier refused to build the cloned third-party code until the
  user approved it.
- `/tmp` cleanup wiped the first scratchpad clone overnight, so the work moved to `~/gitdir/VoyagerSP-macos`.
- The prebuilt FFmpeg `.a` files the Android CMake expects (`ffmpeg/lib64`) are **not in the
  repo**, despite the README. This doesn't matter on macOS (Homebrew FFmpeg is used instead).
- The `HARD_LINKED` and `IN_ClearCrouchToggle` issues above.

## Android-specific code paths (all compile out on macOS unless noted)
- `sys/con_passive.c`: logcat output.
- `sys/sys_main.c`:
  - DEFAULT_BASEDIR = `ANDROID_DATA_ROOT` (/sdcard/Android/obb/com.voyager.efsp);
  - no "abnormal exit / safe video" PID dialog.
- `sys/sys_unix.c`:
  - homepath = ANDROID_DATA_ROOT;
  - SDL message-box Sys_Dialog (macOS uses `sys_osx.m`).
- `sdl/sdl_input.c`, 11 blocks:
  - virtual stick, look area and LCARS touch button overlay;
  - touch menu navigation and crouch toggle;
  - Android back button and gamepad tweaks.
- `client/touch_controls.h` (whole file).
- `client/cl_scrn.c`: touch overlay drawing.
- `client/cl_main.c`, 5 blocks.
- `sdl/sdl_glimp.c`, 3 blocks: surface and orientation.
- `renderervk/vk.c`, 6 blocks: Mali/PowerVR MSAA and device-lost workarounds.
- `qcommon/common.c`, 2 blocks.
- `qcommon/files.c`: in-app import of pk3s plus `efgamex86.dll` through the system file picker.
- `sp/*.c|cpp`: the android/log.h include (shimmed) and the `.so` module names (mapped).
- The Java/Gradle app (`EFAndroid-SP/app/src`): SDLActivity, the file-picker importer and the
  manifest. None of it is needed on macOS (since removed from the tree, with the Android SDL and
  FFmpeg copies).

## Remaining work for a playable macOS .app (estimate: about 4–8 focused days)
1. **Real-data bring-up (1–3 days, the unknown).** Run the retail paks and play through the first
   missions, then fix what breaks. Likely areas:
   - the Vulkan renderer on MoltenVK. It reported API 1.0 and max texture 2048 (the renderer's
     own caps). The `VK_KHR_portability_subset` device extension is not enabled; it should be.
   - Mac/desktop input defaults. Keyboard and mouse bindings and menu mouse handling were tuned
     for touch and gamepad.
   - The window and fullscreen cvar path: `r_fullscreen 0` from cfg was ignored, and it opened
     2560×1440 fullscreen.
   - savegames (the transcoder should be fine on LP64).
2. **.app bundle (1 day).** Adapt lilium's `make-macosx-app.sh`:
   - put `efsp`, both dylibs, `libSDL2`, `libvulkan`, `libMoltenVK` and the ICD json in
     Contents/MacOS or Frameworks;
   - set an rpath, or have `SDL_VULKAN_LIBRARY` / `VK_ICD_FILENAMES` set at startup;
   - ad-hoc codesign;
   - pick a data location: `~/Library/Application Support/…/baseEF`, matching lilium.
3. **Bink cinematics (0.5 day).** Verify `bink_ff.c` with FFmpeg 9 on real `.bik` files; bundle
   or statically link FFmpeg.
4. **Optional: aarch64 JIT (0.5–1 day).** `vm_aarch64.c` needs `MAP_JIT` and
   `pthread_jit_write_protect_np` on macOS. The interpreter may be fast enough on Apple Silicon,
   since the SP game is native and only the menus or bots use the QVM, if at all.
5. **Cleanups.** Build-type string says "debug"; `-w` hides warnings; a CMake install/bundle target.

License note: `efgame/` is under Raven's STEF Game Source License (not GPL). It's fine for
personal builds, but distributing a combined .app is a separate decision.

## Later: Holomatch in the same app (2026-10-23)

The SP engine never stopped being the Holomatch engine: VoyagerSP-Android grafted the SP game
onto lilium-voyager and kept the Holomatch paths behind `sp_ui` / `sp_dropHM`, and the
network code (`msg.c`, `net_chan.c`, `sv_client.c`, `sv_snapshot.c`, `cl_parse.c`) is
byte-identical to lilium-voyager's. So multiplayer is a launch-time mode of the same binary,
not a second engine:

- `com_holomatch` (CVAR_INIT | CVAR_PROTECTED, command line only; `Com_Holomatch()`) selects
  the mode for the whole run.
- In Holomatch mode: the config is `hmconfig.cfg` (`Q3CONFIG_CFG`); the native SP UI is not
  loaded (retail `vm/ui.qvm` instead); `SP_RegisterCommands` is skipped, so the stock
  `map`/`save`/`load` commands stay; `sp_dropHM` is forced 0 (ROM); the SP player-model
  userinfo cvars are not registered; the SP boot logo (`CINEMATICS_LOGO`) is not played;
  networking is on (`net_enabled 3`, as lilium). Single player keeps the SP port's
  `net_enabled 0` (ROM): no sockets at all.
- The window title names the mode (`sdl_glimp.c`).
- `sys_osx_launcher.m` (ARC) picks the mode, graphics, player name and game folder before
  `Com_Init` and appends `+set` arguments; `sys_osx_data.m` provides the data lookup,
  chooser and the File-menu relaunch (`open -n` from an `atexit` handler).
- Holomatch's QVMs run interpreted (`NO_VM_COMPILED`): with four bots on `hm_voy1` the
  server frame is ~0.6 ms and the whole frame ~2.6 ms on an M1 Pro, so the JIT isn't needed.
- Tested: Mac-to-Mac on one machine over the LAN address (broadcast discovery via
  `localservers`, connect, play). Not yet tested against PC Elite Force 1.2 or lilium-voyager.

### Brightness and map downloads (2026-10-23)

- **Brightness is live.** renderervk applies `r_gamma` in the fbo gamma post-pass. It picks
  up the change through Quake3e cvar groups, which the client used to stub out (now
  `CL_CvarSetGroup` / `CL_CvarCheckGroup` / `CL_CvarResetGroup` in `cl_main.c`). The
  launcher always passes `r_fbo 1`, and `r_fbo` defaults to 1 on macOS. With fbo the
  renderer reports `deviceSupportsGamma`, so the SP and Holomatch UIs skip their
  vid_restart. With `r_fbo 0`, gamma is baked into textures and still needs a vid_restart.
  SDL hardware gamma ramps don't work here: the bundled SDL2 is sdl2-compat 2.32 on SDL3,
  and `SDL_SetWindowGammaRamp` returns "That operation is not supported". Both UIs write
  `r_gamma` on every slider change, and SP sliders can now be dragged.
- **Map downloads (Holomatch).** `cl_allowDownload 1`, `sv_allowDownload 1` and
  `sv_dlRate 4000` are the defaults. A one-time `com_hmNetDefaults` stamp applies them over
  configs that archived the old values. Downloads use UDP only (no cURL). On a LAN the
  server sends several rounds of blocks per pass and the client acknowledges every 10 ms.
  A 3.3 MB map takes about 2 s. Single player keeps `net_enabled 0`.
