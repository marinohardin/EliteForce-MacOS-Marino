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

### Dedicated server (2026-10-24)

`+set dedicated 1` (LAN) or `+set dedicated 2` (internet: heartbeats to `sv_master1..5`)
runs a headless Holomatch server from the same executable. Contract for a parent app
(the launcher drives it through `NSTask` pipes):

```
"Elite Force.app/Contents/MacOS/Elite Force" +set dedicated 1|2 +set net_port N
    +set sv_hostname "..." +set g_gametype N +set sv_maxclients N +set g_password "..."
    [+set fs_basepath <folder holding baseEF>] [+exec <rotation.cfg>] +map <map>
```

- `dedicated` implies Holomatch: `+set com_holomatch 1` is added after the arguments
  (a `com_holomatch 0` is overridden; single player has no server mode). Passing it
  anyway is harmless. Single player without `dedicated` keeps `net_enabled 0`.
- Headless: `sys_headless` (`sys_main.c`) is set from the arguments at the top of `main()`.
  `CL_Init` is skipped (`cl_running` stays 0), so there is no SDL video, window,
  renderer, sound, input or local client, and the process never registers with
  LaunchServices (no Dock icon). No launcher, game-folder chooser, File menu or
  `Sys_MacFrame`; `Sys_Dialog` prints to stderr and answers No/Cancel.
- Game data: from the .app it is found as for the game (remembered game folder, the
  homepath copy, next to the app; `Sys_MacHeadlessGameData`), or set with
  `fs_basepath`. With none, one `ERROR: no Elite Force game data: ...` line goes to
  stderr and the exit status is 1; other fatal errors (e.g. a bad `fs_basepath`)
  print to stderr and exit non-zero (3). Never a dialog.
- Console: commands are read line by line from stdin, a TTY or a pipe (`con_tty.c`
  buffers partial reads and splits several lines per read). Output is plain text
  (color codes stripped) on stdout, line buffered. Server commands work as usual
  (`status`, `map ctf_voy1`, `addbot munro 3`, `kick munro`, `say hello`,
  `vstr nextmap`, `quit`...). EOF on stdin just stops reading.
- Ready line, printed after every map load (first `+map`, map changes, rotation):
  `Server ready: map <map> port <udp port> gametype <n> maxclients <n> dedicated <1|2> hostname <name>`.
  The hostname is last (it may hold spaces; color codes stripped). The port is the
  one bound: if `net_port` is taken the engine moves up to 9 ports and reports that.
  IPv6 uses `net_port6`, which defaults to `net_port` on a dedicated server.
- Quit: `quit`, SIGTERM or SIGINT (the `Sys_MacQuitSignals` dispatch sources) shut the
  server down cleanly (`Received signal 15, quitting`, exit 0). SIGPIPE is ignored; a
  write to a closed stdout pipe (the parent is gone) quits the same way.
- Files: the server reads and writes `hmconfig_server.cfg` (not the player's
  `hmconfig.cfg`) and does not write the CD key. Its PID file is
  `Lilium Voyager SP_server.pid`, so a server and a game in the same homepath don't
  show each other the "abnormal exit" dialog (a headless server never shows it).
- LAN discovery (`localservers`, the Holomatch server browser) only probes ports
  27960–27963 (`NUM_SERVER_PORTS`); a server on another port is reached with
  `connect <ip>:<port>`. Discovery also needs the Local Network permission.
- Tested with two scratch HOMEs on one Mac: a server on 29960 and a client connecting
  to the en0 address; `localservers` finds a server on 27962; `+exec` map rotation with
  `vstr nextmap`; `dedicated 2` heartbeats; SIGTERM; no game data.

### Pointer capture in a window (2026-10-24)

Symptom: in game, windowed, the pointer left the window and clicked on the desktop
while mouse look still worked. The engine side was right in every transition
(`in_debugMouse 1` logs each capture/release; game/menu/console/vid_restart/focus
loss all captured and released as they should). The cause is in SDL 3.4's Cocoa
backend (sdl2-compat): a mouse-down outside the content view (title bar) sets the
window listener's `focusClickPending`, and `windowWillMove` sets `isMoving`; both are
cleared only when the matching mouse-up reaches NSApp / the window, which AppKit's
title-bar and window-drag tracking can swallow. While either is set,
`Cocoa_SetRelativeMouseMode` returns early (no
`CGAssociateMouseAndMouseCursorPosition(NO)`, no cursor hide) and the grab sets no
`mouseConfinementRect`, but SDL still reports relative mode and grab as on and keeps
sending deltas. Reproduced by setting `focusClickPending` on a captured window: SDL
said relative 1 / grab 1, but the cursor was visible and the confinement rect empty.

Fix (`sdl_input.c`, `IN_CocoaClearStalePointerState`): whenever the game wants the
pointer captured and SDL's listener reports a drag / title-bar click pending while no
mouse button is down, clear it with SDL's own `windowDidFinishMoving` /
`clearFocusClickPending:` and capture again. `in_mousestate` prints the engine and
SDL2/SDL3 state, the pending flag, the confinement width and whether the system
cursor is visible.

### Host a Server (2026-10-24)

The launcher's third card, **Host a Server**, runs a Holomatch dedicated server from the app
(`sys_osx_server.m`, ARC; shared declarations in `sys_osx_launcher.h`).

- **Main window.** With Host selected, a Server group summarizing the settings (name, game,
  rotation, players and bots, network, the LAN address) replaces the Graphics group at the same
  height, and Play becomes Start Server. Config… is in the card. The window is 784 pt tall.
- **Config… sheet** (Maps, Game, Server). The map list comes from the pk3s in the game folder and
  the homepath's baseEF, read without the engine: central directories, `maps/<name>.bsp`, arena
  info (`scripts/arenas.txt`, `scripts/*.arena`: long name, `type`), levelshot thumbnails
  inflated with the bundled zlib. Retail-pak maps without arena info are single-player maps and
  are left out. Maps get marked as Expansion Pack (pak3.pk3: clients need the pack, servers never
  send it), Add-on, "not made for <game type>", or missing (in the rotation, no longer in the
  files: skipped).
- **Retail rules**, from the Holomatch `ui.qvm` (`vm/ui.qvm` in pak2.pk3; its Create Server code
  disassembled): `g_gametype` 0 FFA, 1 Tournament, 3 Team Holomatch, 4 CTF. Assimilation needs
  Team Holomatch; Elimination isn't for CTF; Action Hero is only for FFA and Tournament.
  Assimilation excludes Disintegration, Elimination and Action Hero; Specialties exclude
  Disintegration and Action Hero; Disintegration excludes Specialties and Action Hero;
  Elimination excludes Action Hero. qagame itself doesn't clear conflicting `g_pMod*` cvars.
  Bots: `bot_minplayers` (per team in team games, checked with the retail qagame) and
  `g_spSkill` 1–5 (Cadet … Captain).
- **Settings** are kept in the app's defaults (`ServerSettings`) and written at each start to
  `baseEF/vsp_server.cfg`, with `set` (not `seta`) and the rotation as
  `set vsp_mapN "map X ; set nextmap vstr vsp_mapN+1"` followed by `vstr vsp_map1`.
- **Server window.** The app's own executable runs as an `NSTask` child:
  `+set com_holomatch 1 +set dedicated 1|2 +set net_port P [+set fs_basepath …] +exec vsp_server.cfg`,
  with stdin/stdout piped and the `VOYAGERSP_TEST_*` variables removed from its environment. The
  window shows the LAN address (getifaddrs, en0 first), map, game, players (a hidden `status`
  every 2 s, parsed), the console and a command field. It also reads "Server ready: …" and
  "Opening IP socket: …:port" (when the port was taken). Play on This Mac opens a second
  instance (`NSWorkspace`, new instance, same environment) with the launcher's graphics, the
  name, the password, and `+connect 127.0.0.1:P`. Stop sends `quit`, then SIGTERM after 5 s,
  then SIGKILL after 3 more. Closing the window stops the server and returns to the launcher;
  Quit stops it and quits. A server that exits on its own leaves the window open with its output.
- **App menu during the launcher.** SDL's About/Hide/Quit items did nothing during the modal
  sessions. They are now retargeted to an object that works when modal while the launcher or
  the server window is up.
- **With the headless server** (srv-engine): the server window reads its "Server ready: map …
  port …" line (after every map load; the port is the one bound, which Play on This Mac uses),
  and explains exit 1 (no game data) and 3 (bad `fs_basepath`). The server keeps its own
  `hmconfig_server.cfg`, so it no longer clashes with a local game. Holomatch's LAN search only
  probes ports 27960–27963; other ports work by address only, and the launcher warns about them.
