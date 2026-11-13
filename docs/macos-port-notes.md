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
  configs that archived the old values. Downloads try HTTP first since 1.3.0 (below). Over UDP, on a LAN the
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

### Master servers (2026-10-26)

- **Slots.** `sv_master1`–`N` serve both directions: a `dedicated 2` server heartbeats every
  non-blank one, and the Join menu's Internet 1–5 run `globalservers 0`–`4`, which the
  ELITEFORCE branch of `CL_GlobalServers_f` maps to `sv_master1`–`5`. The retail ui.qvm offers
  only five sources. `MAX_MASTER_SERVERS` is now 16: slots 6+ are announce-only (and reachable
  with `globalservers 5 24` …), not archived, so configs keep their old shape.
- **Defaults** (`EF_DEFAULT_MASTERS`, qcommon.h; `VSP_DefaultMasters` mirrors it): daggolin.de,
  tjps.eu, stvef.org, Raven's, ipv6.tjps.eu, from the community list on the
  mobile-infanterie.de wiki. 1.2.1 put daggolin first: stvef.org takes no heartbeats and its list
  is a lagging copy of daggolin's (a new server shows on daggolin and tjps, not stvef). Configs
  keep whatever order they have; the launcher passes its list (the defaults unless edited). Before, slot 1 was Raven's (dead and unarchived) and slot 2 tjps.
  All five are archived now.
- **Migration** (`com_masterDefaults` 1, `Com_ExecuteCfg`, Holomatch only): where slot 1 is
  blank or Raven's, slot 2 blank or tjps and 3–5 blank, the new five are set once. Any other
  list is the player's and stays.
- **Launcher.** The list lives in the app's defaults (`MasterServers`; absent means the
  recommended defaults) and is edited in the Servers… sheet (`VSPMastersController`, in
  sys_osx_server.m next to `QueryMaster`, which now takes `host:port` and IPv6). Sixteen
  `+set`s don't fit the engine's 1024-character command line, so the launcher writes
  `baseEF/vsp_masters.cfg` (all 16 slots, blanks included, so a removed master doesn't linger)
  and adds `+exec vsp_masters.cfg` for Holomatch and Play on This Mac; a hosted server gets the
  same lines in `vsp_server.cfg`. The server window's Listed check asks every master in the
  list at once and names each Internet N that lists the server, and those that don't (yet).

## Hosting, loopback and the server browser (2026-10-26)

- **Start Server** opens the server window in an app instance of its own (`--host-server`,
  `sys_osx_launcher.m` → `VSP_RunServerWindow`), so the launcher stays open and can start the
  game while the server runs. Each instance records itself in
  `EliteForce-MacOS/RunningServers/<pid>.plist` (name, port; stale pids are pruned), which the
  launcher reads to ask before starting a second server. The instance stops its server on its
  own Quit, the Dock's Quit (kAEQuitApplication) and SIGTERM.
- **Loopback Host Fix** (launcher, defaults key `LoopbackHostFix`, on unless set to NO): the
  launcher looks up the public address with an address-only NAT-PMP request and passes
  `+set cl_loopbackHostIP <ip>`; `CL_ServersResponsePacket` (`cl_main.c`, `__APPLE__`) rewrites
  master-list entries with that address to `127.0.0.1`, keeping the port. A server on another
  Mac behind the same router would be rewritten wrongly; the checkbox turns it off.
- **Browse…** (`VSPBrowserController`, `sys_osx_server.m`): Local broadcasts `getinfo` on ports
  27960–27963; Internet N asks master N (`QueryMaster`), Combined (the default tab) asks every
  master at once and merges the lists by `ip:port` (a row's tooltip names the masters listing it);
  then every listed server at once on one
  UDP socket (ping is the round trip), applying the loopback rewrite itself. Join plays
  Holomatch with `+connect` (and `+set password` when the server needs one).

## Update check (2026-10-26)

- **What.** The launcher (`sys_osx_launcher.m`, "Updates") asks
  `api.github.com/repos/marinohardin/EliteForce-MacOS-Marino/releases` (unauthenticated, needs the
  repo public) for the newest published release tagged `macos-v<version>`; drafts and
  pre-releases are skipped, versions compare with `NSNumericSearch`. A newer one shows a badge in
  the header ("Update available", Download… opens the release page). Nothing is downloaded or
  installed; no Sparkle.
- **When.** At most once a day (`UpdateLastCheck`); the last answer (`UpdateLatest`) shows at
  once on the next start. A failed check is quiet and retried next start. The app menu gets
  **Check for Updates…** (an alert with the answer) and **Check for Updates Automatically**
  (`CheckForUpdates`, on unless set NO) while the launcher is up.
- **Releasing.** Nothing extra: publishing a `macos-v<version>` release (not a pre-release) is
  what users are told about. Keep the tag format; a different prefix isn't seen.
- **Tests.** `VOYAGERSP_TEST_UPDATE=<version>|none` answers instead of GitHub (not saved);
  `VOYAGERSP_TEST_UPDATE_ALERT=<file.png>` runs Check for Updates… and renders the alert.

## GOG's installer (2026-10-26)

- **What.** `sys_osx_gog.c` unpacks the game's `.pk3` files from GOG's offline Windows installer
  (Inno Setup 5.6.2, Unicode), so Mac-only players can use the GOG download. **Choose…** and
  **Add Game Files…** accept a `.exe`; it is always unpacked into the app's data folder, never
  used in place. No new dependencies: the setup data is LZMA1 (system `/usr/lib/liblzma.5.dylib`,
  declared by hand since the SDK has no header), file data is zlib (the engine's bundled zlib),
  hashes are CommonCrypto.
- **Format.** Inno's offset table (`rDlPtS\xcd\xe6\xd7\x7b\x0b\x2a`) points at the setup data:
  two CRC-checked LZMA1 blocks (header + entries; 74-byte file locations). GOG doesn't use Inno's
  own files for the game: each file is split into ≤10 MB parts, each zlib-compressed and stored
  raw; the entries' scripts carry `before_install('<file md5>', 'BaseEF\<name>.pk3', <parts>)`
  and `after_install('<part md5>', <stored>, <size>)`. The extractor finds those strings, maps each
  to its location entry, and only accepts `BaseEF\*.pk3`.
- **Checks.** Every block CRC, each part's SHA-1 (stored data) and MD5 (unpacked), and the whole
  file's MD5. Anything unexpected (not a PE, not Inno 5.x Unicode, truncated, a part that fails a
  check, an unknown layout) stops with an alert naming the problem and asking for a `BaseEF` folder
  from a Windows install; partly unpacked files are removed. Verified with GOG's current 1.2 installer:
  pak0–2 byte-identical to a PC install, about 7 s for 683 MB.
- **Tests.** `VOYAGERSP_TEST_ADD=<installer.exe>` / `VOYAGERSP_TEST_CHOOSE=<installer.exe>`.
  Never commit an installer or anything taken from one.

## Join links and the Humans column (2026-10-27)

- **stvef:// links.** `Info.plist` claims the `stvef` URL scheme (`CFBundleURLTypes`), so
  efservers.com's Join links open the app. Formats, as in
  [stvef-protocol](https://github.com/stefansundin/stvef-protocol): `stvef://connect/<host>:<port>[?…][#…]`
  and the older `stvef:<ip>;<port>;<name>;<map>;…`; only the address is used, and it must match
  `host:port` (letters, digits, `.`, `-`, or `[IPv6]`) since it ends up on a command line.
  `sys_osx_launcher.m` installs a `kAEGetURL` handler right after SDL's video init (SDL installs its
  own there, which it would otherwise keep). With the launcher up the link is Join (Holomatch,
  straight in, closing any sheet); in a Holomatch game it is `connect <address>`; in single player
  a dialog says it can't join. Verified with a cold start through LaunchServices and a second
  link while in game, both against local servers. Test hook: `VOYAGERSP_TEST_LINK=<link>`.
- **Players / Humans.** Players is `clients/sv_maxclients` (bots included); Humans is
  `g_humanplayers`, or `?` when the server doesn't send it. Default sort: Humans (`?` as 0), then
  Players, then ping.
- **Favorites and filters.** A star column (`BrowserFavorites`, addresses) sorts first; **Only
  favorites** and **Only servers with people** (`BrowserOnlyFavorites`, `BrowserOnlyPeople`)
  combine. "With people" keeps `?` servers that have players, since they may have people.
- **Who's playing.** Selecting a server sends that one server a `getstatus` (up to two tries,
  1 s each) and lists `<score> <ping> "<name>"` with its colors, people (a ping) before bots, then
  by score; nothing is sent to the others.
  Answers are kept for the sheet's life (the small refresh asks again). Bots have ping 0, the only
  tell: rows get a "bot" tag only when the count of players with a ping matches `g_humanplayers`
  (from the answer, else from `getinfo`), or when the server doesn't say.
- **Map Info and 3D Map Preview.** For maps that aren't the game's own (pak0-pak3 and the
  Expansion Pack list), a sheet with a `WKWebView` (WebKit, a system framework) shows
  `efservers.com/pk3.php?preview&popup=1&map=<map>` (Map Info: screenshots, files), with Open in
  Browser. On selecting such a server, `efservers.com/getPK3info.php?pk3=<map>&preview` is fetched
  once per map; when it links `bsp/index.php?pk3=…&file=maps/….bsp`, **3D Map Preview** shows
  that page the same way. A sheet, since the launcher is app modal. The globe button opens
  efservers.com.
- **Join links owner.** When `URLForApplicationToOpenURL:` says another app (cMod claims `stvef`
  too) opens `stvef://`, a row offers **Open Join Links with Elite Force**
  (`setDefaultApplicationAtURL:toOpenURLsWithScheme:`, macOS 12+; macOS may ask to confirm).
  Without a choice, macOS picks among the apps that claim it, often the most recently registered.
- **Tests.** `VOYAGERSP_TEST_BROWSER_SELECT=<row>` (and waits for its players),
  `VOYAGERSP_TEST_BROWSER_PREVIEW=<png>`, `VOYAGERSP_TEST_LINK_HANDLER=<app>`.

## HTTP map downloads (2026-10-27)

- **HTTP first, UDP fallback.** Most live servers set `sv_dlURL`; the client now uses it.
  `USE_CURL` builds `client/cl_curl.c`, linked straight to the macOS system libcurl (`-lcurl`,
  `/usr/lib/libcurl.4.dylib`, SDK headers through `USE_SYSTEM_CURL` in `cl_curl.h`). No
  dlopen, no `cl_cURLLib` cvar, nothing bundled. `cl_allowDownload 1` (the default; bit 2,
  `DLF_NO_REDIRECT`, unset) already allows HTTP, so the `com_hmNetDefaults` stamp is
  unchanged. The client asks `<sv_dlURL>/baseEF/<map>.pk3`, disconnects while it downloads (as
  ioq3 does, unless the server sets `DLF_NO_DISCONNECT`), then reconnects.
- **A wrong sv_dlURL doesn't keep you out.** Stock ioq3 drops with "Download Error" when the
  HTTP download fails (404, connection refused). Here, when UDP is allowed on both sides, the
  client deletes the partial file, notes the server (`cls.cURLFailed`), reconnects and gets
  the files over UDP; that note is cleared once the download list is done. An empty
  `sv_dlURL` goes straight to UDP.
- **cMod's `CMOD_DL_LASTMSG_FIX`** (no `MSG_ReadData` for an empty last chunk in compat mode)
  is already in `CL_ParseDownload`: lilium-voyager's `if (size)` does the same.
- **Single player** never reaches this: downloads start only from a Holomatch gamestate, and
  SP runs with `net_enabled 0`.
- **Tested** against a local dedicated server and `python3 -m http.server`: HTTP download
  (byte-identical pk3, map loads), a 404 URL (warning, reconnect, UDP), and an empty URL
  (UDP). Note: on the command line `+set sv_dlURL http://...` is cut at `//`; set it in a cfg.

## MP3 music: skip non-Layer III frames (2026-10-27)

`S_MP3_Scanfile` gave up on the whole file at the first header that wasn't Layer III, so a
stray false sync broke the music (cMod names `amenhotep` and `hm_stronghold`). It now skips
that frame (`CMOD_MP3_LAYER_CHECK_FIX`, from cMod by Noah Metzger (Chomenor)). Checked with a harness running the
codec over an SP track with a bogus Layer II frame in front: the old code fails to open it,
the new one decodes all of it. All 49 music MP3s in pak0, pak3 and qbeast decode the same as
before, and SP music (borg1) still plays.

## Server, sound and stability fixes from cMod by Noah Metzger (2026-10-27)

Every item below is from cMod (ioef-cmod) by Noah Metzger (Chomenor), ported as plain code,
with his credit and the cMod switch name in a comment. Holomatch servers (Host a Server,
dedicated) and single player share this code.

**Server** (all from cMod by Noah Metzger):
- `getstatus` advertises `version "<ours> HM v1.20 compatible"` (the real one is in
  `version_engine`) and, when a master asks with a challenge, `gamename baseEF`; `getinfo` sets
  `challenge` last, so it comes first (`Info_SetValueForKey` prepends). (`CMOD_GETSTATUS_FIXES`)
- Every protocol mismatch gets the "Server uses protocol version 24." reply old ioEF clients
  look for. (`CMOD_PROTOCOL_MSG_FIX`)
- Holomatch no longer resets `nextmap` on map load (rotations that set it once stuck on one
  map); single player keeps the old behavior. (`CMOD_NO_ENGINE_NEXTMAP_SET`)
- `sv_minSnaps` (default 50, still capped at `sv_fps`). (`CMOD_MIN_SNAPS`)
- Bots and the listen server's player get `g_password` in their userinfo, so bots join
  passworded servers. (`CMOD_BOT_PASSWORD_FIX`)
- The botlib action flags use EF's game-code values and `BOTLIB_EA_ALT_ATTACK` calls
  `EA_AltAttack`, so bots alt-fire. (`CMOD_BOT_TWEAKS`, engine side)
- Gamestates that would overflow drop the entity baselines that don't fit
  (`client_t.baselineCutoff`, null baselines in snapshots). (`CMOD_GAMESTATE_OVERFLOW_FIX`)
- `map_restart` keeps the serverid and ignores moves sent before it (`mapRestartSequence`).
  (`CMOD_MAP_RESTART_STATIC_SERVERID`)
- A client keeps the pk3 list from its gamestate (`downloadPakNames`), so a map change during a
  download of several pk3s no longer refuses the rest. cMod's version uses its new filesystem;
  this one works on `files.c`'s referenced-pak list. (`CMOD_PER_CLIENT_DOWNLOAD_MAP`)
- `map_restart` skips clients still downloading; legacy-protocol downloads stay `CS_CONNECTED`
  and don't delta from the dummy snapshots. (`CMOD_DOWNLOAD_PROTOCOL_FIXES`, the parts our
  monotonic dummy-snapshot time doesn't already cover)
- The dead EF auth server is never contacted (server challenge, `ipAuthorize`,
  `banUser`/`banClient`, client `getKeyAuthorize`): those blocks are `!defined(ELITEFORCE)`.
  (`CMOD_DISABLE_AUTH_STUFF`)

**Sound** (from cMod by Noah Metzger):
- On map load (`CL_StartHunkUsers`) `S_ResetStaleSounds` unloads cached sounds whose file now
  comes from another pk3 or folder (`FS_FileSource`, `S_CodecSoundSource`); they reload when
  used. Base backend only; OpenAL isn't covered. (`CMOD_SOUND_RESET` / `CMOD_FAST_SOUND_RESET`,
  rewritten for our filesystem)
- Twice id's per-entity concurrent sound limits. (`CMOD_CONCURRENT_SOUNDS`)
- 64-bit mixer index. (`CMOD_SOUND_DMA_BUFFER_TWEAK`)
- Not needed here: `CMOD_SOUND_DISABLE_SDL_FORMAT_CHANGES`, since sdl2-compat already hands back
  the requested format (S16, 22050 Hz) with `SDL_AUDIO_ALLOW_ANY_CHANGE`.

**Stability** (from cMod by Noah Metzger):
- 1024 queued events, was 256. (`CMOD_EVENT_QUEUE_SIZE`)
- Overflow checks in the legacy (`compat`) bit reader/writer in `msg.c`.
  (`CMOD_MSG_OVERFLOW_CHECKS`)
- Out-of-range VM block copies wrap instead of `ERR_DROP` (Super FFA).
  (`CMOD_VM_TOLERANT_BLOCKCOPY`)
- `VM_Strncpy` for the strncpy syscalls (macOS aborts on overlapping `strncpy`).
  (`CMOD_VM_STRNCPY_FIX`)
- A non-inlined `LittleLong` in `cm_load.c` for bsps with unaligned lumps. (`CMOD_CM_ALIGN_FIX`)
- NaN check in renderervk's `R_FogFactor`. (`CMOD_FOGFACTOR_FIX`)
- renderervk (Quake3e) already had his fog side and FRCL fixes.
## QVM float-to-int rounding (2026-10-27)

cMod's (by Noah Metzger, Chomenor) `CMOD_VMFLOATCAST` (`VM_tonextint` in its interpreter, from Thilo Schulz's game code
patch) makes the VM's `OP_CVFI` round to nearest instead of truncating. Elite Force's
physics depend on that: the retail engine ran QVMs on x87 (`fistp`, round to nearest), and
`bg_pmove.c` snaps the velocity every frame with `SnapVector` (`(int)` casts), so truncation
loses up to one unit per axis per frame (a rough 125 fps model: jump apex ~42 instead of ~48.5).
- **Our build already rounds.** On arm64 `Q_ftol` is `lrintf` (`q_shared.h`); clang inlines it
  as `frintx` + `fcvtzs` in `vm_interpreted.c`, with `Sys_SetFloatEnv` setting
  `FE_TONEAREST`. ioEF does the same on x86 (`asm/ftola.c`: `cvtss2si` and an x87 control
  word of `0x007F` under `ELITEFORCE`). So the interpreter is unchanged apart from a comment.
  The only difference from cMod is an exact .5 (we round to even, as x87 did; cMod rounds
  away from zero), which is too rare to matter.
- **aarch64 JIT** (`vm_aarch64.c`, not built: `EF_VM_JIT` is off) used `FCVTZS` (truncate);
  it now uses `FCVTNS` (round to nearest, ties to even), so turning the JIT on later doesn't
  change the physics.
- Single player is not affected: its game code is native, and the HM qagame frame is skipped
  while `SP_IsActive()` (`sv_main.c`).

## Downloaded pk3s go to `downloads/` and can't override installed files (2026-10-28)

From cMod by Noah Metzger (Chomenor), whose new filesystem keeps auto-downloads in a download
folder and ranks them below everything else (`fs_download_mode`, the `downloads_folder` and
"prefer non-downloaded pk3" precedence checks in `code/filesystem/fs_lookup.c`). Ported to
ioquake3's `qcommon/files.c` search path. Motivating case: a server auto-downloaded cMod's
`pakcmod-*.pk3` into `baseEF/`; sorting last, it replaced the stock QVMs on non-pure servers and
when hosting.

- **Where downloads go**: UDP and HTTP downloads (and their `.tmp` while in progress) are saved
  to `baseEF/downloads/` (or `<mod>/downloads/`) under the home path. `FS_ComparePaks` builds the
  local name; the remote name stays `baseEF/<name>.pk3`, so servers and `sv_dlURL` web folders
  need nothing new. Interrupted `.tmp` files in `downloads/` are deleted at startup
  (`FS_CleanDownloadTemps`, once per launch, not on filesystem restarts).
- **Priority** (`FS_AddGameDirectory`, `FS_DemoteDownloadPaks`, `FS_DownloadPakListed`):
  pk3s in `downloads/` always load, so their maps and models are found, but are moved below
  every other pak and folder, so they only add files nobody else has. While connected to a
  *remote* server whose `sv_paks` or `sv_referencedPaks` lists one (pure or not), that pak takes
  its normal place, sorted by name among the game folder's paks. On a pure server the pure list
  order applies as before and finds paks in `downloads/`. When this app runs the server (local
  Holomatch hosting, Host a Server, single player) downloads never get promoted. Leaving a server
  restarts the filesystem if a downloaded pak had been promoted. `path` marks each downloaded
  pak.
- **Names and checksums**: a pak in `downloads/` keeps its plain name and game folder, so it is
  reported as `baseEF/<name>` with the same checksums. Pure checks pass and nothing is
  downloaded again.
- **Serving**: a server that has a referenced pk3 only in its own `downloads/` serves it over UDP
  as `baseEF/<name>.pk3` (`SV_OpenDownloadPak`).
- pk3s you put in `baseEF/` yourself are unchanged: they load and override as before.

**Existing downloads** in `baseEF/` (from earlier versions) are not moved or deleted. To get the
new behavior for them, quit the game and move the downloaded pk3s from
`~/Library/Application Support/EliteForce-MacOS/baseEF/` into `baseEF/downloads/` (create the
folder). That includes `pakcmod-*.pk3` and any map pk3 you didn't install yourself. Leave
`pak0.pk3`-`pak3.pk3` and the pk3s you added yourself where they are. Leftover `*.pk3.tmp` files in
`baseEF/` are interrupted downloads and can be deleted.
## Brightness settings and presets from cMod (2026-10-28)

From cMod by Noah Metzger (Chomenor), ported into `renderervk` (Vulkan, no OpenGL) in our own
style; credited in each spot in the code.

- **cMod's brightness settings** (`CMOD_MAP_BRIGHTNESS_SETTINGS`, Noah Metzger): `r_overBrightFactor`
  (fractional overbright; the fbo gamma pass's `obScale` takes it, `tr.identityLight` follows),
  `r_mapLightingFactor`, `r_mapLightingGamma`, `r_mapLightingGammaComponent`,
  `r_mapLightingClampMin/Max` (`R_SetMapLighting` / `R_ShiftMapLighting` in `tr_bsp.c`, applied to
  lightmaps, vertex light and the light grid through `R_ColorShiftLightingBytes`). Integrated with
  Quake3e's own handling: the two factors default to `0`, meaning "follow `r_overBrightBits` /
  `r_mapOverBrightBits`", and settings the old integer shift can express keep using it, so the
  default look is bit-for-bit the old one (`gfxinfo`/R_Init prints `MAP LIGHTING: ... (overbright bits)`).
- **`r_textureGamma`** (`CMOD_TEXTURE_GAMMA`, Noah Metzger): gamma on textures at load, not lightmaps.
- **Fractional `r_intensity`** (`CMOD_FRACTIONAL_INTENSITY`, Noah Metzger), used by two presets:
  `1.1:0.5` scales only the lower half of the range. `r_intensity` lost its numeric range check
  (it would reject the `:`); the value is clamped to 1..255 where it is used.
- **Map auto-adjust** (`CMOD_MAP_AUTO_ADJUST`, Noah Metzger's `cmod_map_adjust.c`): before the
  renderer starts for a map, `CL_MapAutoAdjust` (`cl_main.c`) looks the .bsp up in cMod's list of
  known maps (by checksum) or checks it for 3+ kinds of Quake 3 items, and sets the ROM cvars
  `r_autoMapLightingFactor`, `r_autoMapLightingGammaMod`, `r_autoOverBrightFactorMax/Shift`. EF's
  own maps match neither. `cl_mapAutoAdjust 0` turns it off. cMod's Quake 3 environment-map mode
  (`r_autoEnvMapMode`) and external-lightmap processing are not ported.
- **Launcher**: a Brightness popup in Graphics: Standard (this port's look, the engine defaults)
  and cMod's Plain, Full Contrast, Extra Bright and Full Bright templates (values from cMod's
  `ui_video.c`). It reads the mode's config like the other graphics settings; a config that
  matches none (the in-game Brightness slider moved, say) shows Custom and Play passes nothing.
  Play passes `+set`s; if the 1024-char command line would overflow, they go to
  `vsp_brightness.cfg` and `+exec` instead (then they apply from the first map on). Test hook:
  `VOYAGERSP_TEST_LAUNCHER_BRIGHTNESS=standard|plain|contrast|bright|fullbright`.
- **Tested**: hm_kln1 at default vs a cb80018 build (same within frame-to-frame noise), each preset
  (engine `screenshot`, with `r_ext_supersample 1` so the capture includes the gamma pass), a test
  copy of hm_kln1 with Quake 3 items added (auto-adjust on, reset on the next map), single player
  voy3 at default (pixel-identical to cb80018) and Plain. No Quake 3 port is in the data folder.
- **Note**: without `r_ext_supersample`, renderervk's `screenshot` reads the frame before the gamma
  pass, so `r_gamma` and the overbright factor don't show in screenshots (unchanged, as before).
## 2026-11-01: Adaptive packet duplication

Our own feature (not from cMod). `client/cl_input.c`, `cl_adaptivePacketdup` (archived, default 1;
0 is the old behavior). While packets are being lost, each client packet repeats the usercmds of more
earlier packets, so a lost packet doesn't lose input. Client only, no protocol change: the server
already skips usercmds it has seen and takes up to `MAX_PACKET_USERCMDS`. The effective count is
local (`cl_packetdupEffective`, read only, not archived; `developer 1` prints each change with the
estimates). `cl_packetdup` is the floor and is never written; 5 is the ceiling; the count is lowered
if a packet would overflow 32 usercmds (low `cl_maxpackets`, high frame rate).

- **Loss signals, over 8 s (1 s buckets), the higher one wins.** The client can't see outbound loss
  directly. Inbound: `clc.netchan.dropped`, exact for server to client but only a proxy for client to
  server. Outbound estimate: a snapshot's `ps.commandTime` names the newest usercmd the server ran;
  if the packet sent after it is older than the recent minimum ping plus a margin (a client frame +
  4 ms), it was lost. Weighted by the share of losses that margin leaves visible; not used when that
  is under 25% (very high packet rates). Jitter can pass for loss, so this signal alone stops at 3.
  An inbound run of n lost packets asks for n.
- **Loss to count:** 1% 1, 3% 2, 8% 3, 15% 4, 25% 5. **Rises at once**, falls one step per 4 s, and
  not while the loss would still ask for the value at half again as much (no flapping at a threshold).
- **Not loss:** nothing is sampled outside `CA_ACTIVE` or for 1 s after it, after a 500 ms silence,
  while following another player, or for a gap of a whole `PACKET_BACKUP`. Reset at each gamestate
  and on disconnect. No effect on loopback (hosting and playing in one app) or in demos; single
  player never reaches `CL_WritePacket` in play.
- **`net_dropsim` drops incoming packets only, in whichever process sets it** (`NET_Event`): on the
  client it tests the inbound signal; on the server (via rcon) it tests the outbound estimate.
- **Measured** against an own dedicated server, turning and moving, 30 packets/s, payload bytes per
  packet: no loss, dup 1, 31 B (936 B/s). Client 5%: dup 2-3, 40 B. Client 20%: dup 4-5, 59 B.
  Client 30%: dup 5, 63 B (1.9 KB/s). Server 5%: dup 2-3, 37 B. Server 20%: dup 3, 49 B. After removing
  loss it was back at the floor in about 20 s. `cl_adaptivePacketdup 0` at server 20%: dup 1, 31 B.
  `cl_packetdup 3`: stays 3 without loss, 4 at 20%, back to 3, and the config still says 3. A map
  change returned to the floor at once, with no rise from its gap. 15 packets/s at 250 fps with 40% loss: up to 31 usercmds
  a packet, no `MAX_PACKET_USERCMDS`. No false rise in the no-loss phases (20-30 s each) on localhost.
- **Not tested:** real internet loss and jitter (`net_dropsim` is random and uniform; bursts and
  jitter are not simulated), a server other than this engine's (server code is unchanged), and
  whether play feels better. Over a jittery connection without loss the outbound estimate may hold
  the count above the floor: compare `cl_packetdupEffective` with `cl_packetdup` on a real server.

## Host a Server: Tick Rate (2026-11-01)

Config… > Server has a Tick Rate choice: Standard (20, the default) or High (40), saved with the
other host settings. High adds `set sv_fps 40` to `vsp_server.cfg`, which covers dedicated hosting
and Play on This Mac (a client of that server). Standard writes no `sv_fps` line. Single player
stays at 20 even with High saved. Test hook: `VOYAGERSP_TEST_SERVER_TICKRATE`.

`sv_minSnaps`, from cMod by Noah Metzger (Chomenor), makes this work for clients with the stock
`snaps 20`. It also raises bots, whose `snaps` is 20. No cMod code was ported for this change.

`sv_fps` is `CVAR_SYSTEMINFO` only under `ELITEFORCE` (Thilo Schulz's patch, no reason recorded), so
clients mirror the value. No engine client code or QVM strings in the data folder read it (retail, `vm.pk3`,
tfgpak, cMod), so this has no measured effect. The client keeps the mirrored value until it quits.

Measured on loopback, one client, Mac server (msg/s, payload bytes/s; add 28 bytes per packet for IP/UDP):

| Case | 20 | 40 |
| --- | --- | --- |
| `snaps 20`, 11 bots | 20/s | 40/s (`sv_minSnaps 0` control: 20/s) |
| `snaps 100`, 11 bots | not run | 40/s |
| Protocol 24 mode of our client, 11 bots | not run | 40/s |
| 31 bots, `rate 25000`, following a bot, hm_voy1 | 1000-1700 B/s | 1950-2600 B/s |
| Same, hm_kln1 | 1400-1800 B/s | 2400-3400 B/s, largest message 377 B |
| Same, `rate 3000` | 15-19/s | 14-24/s (rate limited, no worse) |
| Server CPU, 31 bots (one core = 100%) | 3.8% | 6.4% |
| Jump peak / airtime samples, run speed | 46-47, 246 ups | 47, 248 ups |

Map change, `map_restart 0` and `map_restart 3` keep 40/s with no errors. The real data folder's add-ons
(symlinked read-only) also gave 40/s. Bot kills in 60 s (4 vs 7) are too few to compare.

Known costs of 40:
- The snapshot delta window is counted in packets (`PACKET_BACKUP - 3`), so it shrinks from about 1450 ms to
  725 ms. Beyond that a client gets uncompressed snapshots. From code, not tested with real latency.
- Stock cgame cosmetics, from cMod's comments, not observed: the scavenger primary shot appears closer to
  the muzzle (cMod's `cg_ents.c` delays it) and empty-phaser effects depend on prediction length (cMod's
  `cg_weapons.c` emulates a 50 ms cycle). Those fixes are in cgame, so the engine can't apply them to stock clients.

Test gotcha: `sv_minSnaps` and `sv_lanForceRate` are `CVAR_ARCHIVE`, so a test value persists in
`hmconfig_server.cfg` and affects later runs. A saved low `sv_minSnaps` would also cap High's benefit.

Not tested: the retail Windows client, real latency or packet loss, several human clients, and feel.

## Single player: pause keeps the sound, voy1 music, voice timers, cutscene skip (2026-11-07)

Our own fixes (not from cMod).

**Pause holds the sound instead of stopping it.** The SP bridge used to call `S_StopAllSounds` when
the console, the pause menu or an in-game movie paused the sim, so the music and the current voice line
were lost for good while the line's lip-sync and dialog timer kept running. Now `S_PauseGameSounds`
(`snd_dma.c`, plus an OpenAL version) holds the channels playing when the pause starts and the music
(raw stream 0): they aren't painted or refilled, and on resume their start positions and the music's
buffered tail move forward by the paused time. Sounds started while paused (menu clicks, movie audio)
play normally. The bridge drops the cgame's new sounds while paused, so the frozen frame can't start
ambient blips. The voice timers count level time, which is frozen too. Holomatch never calls it.

**voy1 bridge music.** `inGameCinematic` (`cl_cin.c`) also stopped all sounds, when the movie started
and when it stopped. On the `Autosave - voy1` load the map's music starts, `st_01` (the captain's log)
then killed it, and nothing restarted it until the script set the music again after `st_02`. In-game movies
now pause the game's sounds and their audio has its own raw stream (`S_RAW_STREAM_CINEMATIC`), so the
music resumes after the movie. Bink audio now waits in a FIFO in `bink_ff.c` and goes to the mixer only
while the ring has room. A Bink file's audio starts about 0.8 s before its video, more than the ring holds,
so it used to overflow and overwrite itself. This affects fullscreen movies too: no audio is dropped and the
sound keeps time with the picture.

**Voice timers and Munro's first line.** Every save renames `saves/current.sav` through `FS_Rename`,
which called `S_ClearSoundBuffer` and killed every playing sound. The deferred autosave after a cutscene
did this just as the next line started. `FS_Rename` no longer clears sounds in SP (Holomatch unchanged).
The bridge drops its fake-voice timers when the level clock jumps (a load or transition) or sounds are stopped
(`S_StopAllSoundsCount`). Mouths now move only while a voice plays.

**Cutscene skip (Use Item / ENTER in a camera scene).** The game sets `skippingCinematic 1` and
`timescale 100`. The bridge now scales its sim by timescale while skipping, up to 200 ticks or 12 ms of real time per frame
(the rest carries over), and stops at once when the skip ends or the game queues a movie. Rendering
calls are dropped and a black screen with five LCARS-orange dots is drawn. Game sounds stop at the
start of the skip, but the music keeps playing. A movie, a load or new game (`SP_UnloadGame`) and a disconnect
(`CL_Disconnect`) end the skip. In SP the engine clock ignores `timescale` (`Com_ModifyMsec`), so menus,
console and movies always run in real time. Holomatch is unchanged. Enter outside a camera does what it did before.

Tested (scratch HOME, developer log): console and pause menu mid-dialogue hold 2-3 channels and resume
them, and the dialog waits line up exactly. The music holds across `st_01` and resumes. ENTER in the borg1 opening
skips about 60 s of the scene in 0.3 s with the dots shown, and the game ends playable with `timescale` 1. ENTER in the voy1
bridge skips to `st_02`. Voice timers drop on load and on the borg1->borg2 transition. A save mid-line keeps
the line. A Holomatch local server game was unaffected. Not verified by ear: actual audio output and A/V sync of movies.
## SP Level Start: Settle Frames, View Angles, Out-of-View Loops (2026-11-07)

Three single-player start-of-level problems, all in the SP bridge (`EFAndroid-SP/app/jni/sp/sp_bridge.cpp`).

**Settle frames.** Retail `SV_SpawnServer` runs three 100 ms game frames after `Init` and before any
client connects (stock Q3 `sv_init.c`), and the client only enters the world after loading cgame, while the
server keeps running. The bridge ran one frame and showed the first snapshot at once, so:
- voy1 autosave: NPC `EV_PLAYER_TELEPORT_IN` events (fired at 1100) were still valid (300 ms) in the first
  snapshots, and the crew visibly beamed in. The player also hung in the air.
- New Game / borg transitions: the player was falling at the first snapshot (borg2: z 33 to 16 in 150 ms).

Now `SP_SpawnServer` and eAUTO transitions run `SP_RunFrames(3)` after `Init`, and `SP_SettleClient(4)`
runs four 50 ms ClientThink+RunFrame ticks after `ClientBegin` (with `cmd.weapon` matching `ps.weapon` so
the weapon is not lowered) before the load screen goes. First snapshot is at 1500. eFULL saves skip the
pre-client frames (the save already holds settled state). An eAUTO load keeps the save's `TIME` when it is
later than the reset clock. The `level.time > 1000` gate in `g_target.cpp` is now always passed for fresh
spawns, as in retail.

**View angles.** `cl.viewangles` was never cleared on SP level changes (retail `CL_ClearState` zeroed it),
so the previous level's mouse turn was added to the new spawn angles (borg2 faced -43 instead of -90).
`SP_ResetViewAngles()` (`sp_integration.c`) clears it before `ClientBegin`.

**Forcefield hum after the holodeck.** Retail only sent entities in the viewer's PVS; the bridge sends
every entity, and cgame re-adds every snapshot entity's `loopSound` each frame, so holodeck's three
`borgfieldhum` `target_speaker`s were heard from the corridor and turbolift. `BuildSnapshot` now zeroes
`loopSound` (only) for linked, non-broadcast entities outside the viewpoint's PVS. The viewpoint is the
cgame camera when one is active (`CG_GetCameraPos`, looked up by `dlsym` and exported from `efgame.exp` /
`efgame.ver`), else the player's eye. No area test: the save `PRTS` chunk (areaportal state, numAreas²×4)
is written as zeros and not restored, so area connectivity is wrong after loads. Full entity culling plus
real `PRTS` save/restore is a possible follow-up. A loop seen only through a portal surface may be culled.

Tested on Mac: New Game borg1, borg1 to borg2, voy1 autosave, eFULL loads (borg1, voy1), autosave reload,
hum on/off between the sim room and corridor. Not tested: Android, by ear, the whole prologue played by hand.
## SP display fixes: notify text, in-game video, Hor+ FOV, flares (2026-11-07)

Our own fixes (not from cMod), from the SP player report.

- **Debug text over subtitles.** Renderer diagnostics ("Prewarmed N pipelines", "VKDIAG: ...")
  and the SP bridge's info logs (the autosave's `[EFSP] save: wrote saves/auto.sav ...`, the
  spmap/efsptrans load lines) and the macOS File menu line now start with `[skipnotify]` (RTCW's
  prefix, already handled in `CL_ConsolePrint`): console and `qconsole.log` only. `Com_Printf`
  strips the prefix for the tty, redirects and the log. In `macos/android/log.h`,
  `ANDROID_LOG_INFO` skips notify; warnings and errors still show. `SP_EndLoad`
  (`sp/sp_integration.c`) now calls `Con_ClearNotify()`, as the stock `CL_InitCGame` does.
- **In-game video.** `SCR_DrawScreenField` (`client/cl_scrn.c`) fills the screen black before an
  `inGameCinematic` video, so on a wide screen it is 4:3 with black bars instead of a box over
  the paused level and HUD (SP and Holomatch). Menu/startup videos are unchanged;
  `cl_flexibleDisplay 4` still stretches.
- **Hor+ FOV in SP.** `CG_CalcFOVFromX43` (`efgame/src/cgame/cg_view.cpp`) treats the FOV as the
  4:3 value: same vertical FOV as 4:3, wider sides. Used for camera scenes (the opening crawl
  pictures were losing ~22% of their height at 16:9) and for gameplay `cg_fov`/zoom, which
  matches Holomatch (`cl_flexibleDisplay 2` already does Hor+ in `CG_R_RENDERSCENE`). At 4:3, and
  with `cg_forceAspect 1`, nothing changes. Camera letterbox bars and the crawl scrolling off the
  top are retail and kept.
- **Light flares on.** `r_flares` defaults to 1. Once (`com_flaresDefault 1`, the same one-time
  stamp pattern as `com_hmNetDefaults`), SP and Holomatch configs that still hold 0 get 1; turning
  flares off later sticks.
- **Tested** (1920x1080 and 1440x1080, scratch HOME): "Autosave - voy1" (captain's log video now
  pillarboxed in black, no diagnostics at the top), New Game borg1 crawl (16:9 picture framing
  matches 4:3), forward transition borg1 -> borg2 (clean first frame), `save auto` mid-level (the
  save line stays in the log only), borg2 gameplay 16:9 vs 4:3 (same vertical view and gun size),
  `inGameCinematic st_01` in SP and Holomatch (local server, hm_voy1), startup Activision video
  (unchanged), the flares stamp (0 -> 1 once, a later 0 is kept). Flares cost about 0.3 ms per
  frame in borg2 and 0.6 ms in hm_voy1 at 1080p (uncapped, `com_speeds`; noisy).
- **Not tested**: the in-game options menus showing Light Flares On (they read `r_flares`, so
  by reading the code), Holomatch's own UI, other SP camera scenes, 21:9 screens, and feel.

## Regression harness: `macos/regress.sh` (2026-11-07)

Our own tool (not from cMod). It checks that a change leaves single player and Holomatch game logic
exactly as they were. It runs fixed scenarios and logs the game state every frame. Then it compares that
log with a baseline recorded from another build.

**Usage** (also at the top of the script):

    macos/regress.sh record [NAME]   # run all scenarios with the current build -> baseline NAME
    macos/regress.sh check [NAME]    # run them again and compare (default: the newest baseline)
    macos/regress.sh list

`NAME` defaults to `git describe --always --dirty`, so each commit or build gets its own baseline.
Baselines live in `~/Library/Caches/VoyagerSP-regress/` (`REGRESS_DIR`), never in the repo. The usual
workflow: build the base commit and `record`, then build your change and `check`. The script assembles its
own "EF Test Regression" app (`org.voyagersp.testregress`) from `build-macos` and `efgame/buildmac`
and deletes it afterwards. To use your own test app, pass `APP=<path.app>`. `SCENARIOS="sp-save hm-listen"`
runs a subset. Each run gets a fresh scratch HOME with symlinks to pak0-pak3. Nothing touches the real
game folder, its configs or its saves. `check` prints `identical` per scenario, or the first
frame that differs with each changed entity and field (`health: 60 -> 55`). It exits 1 if anything
differed or a run didn't finish.

**Scenarios.** `sp-borg1`: new game on borg1, 2000 frames (the intro cutscene, ICARUS, NPCs).
`sp-save`: a voy1 save, 600 frames of scripted play. By default it uses your newest voy1 save. `record`
copies it into the baseline, and `check` loads that copy. `sp-transition`: borg1, `maptransition borg2`
at frame 40, to frame 400. `hm-listen`: a Holomatch listen server on hm_voy1 with 3 bots and the
local client connecting and playing, 1200 frames. `hm-dedicated`: a dedicated server, 3 bots, port
27980, 1200 frames.

**Engine hooks** (`qcommon/regress.c`). They are off unless the command line sets `+set regress_log <file>`
(CVAR_INIT). When it's off, every hook returns at once. When it's on:
- **Time step.** Each engine frame is exactly one 50 ms game frame (`Com_ModifyMsec`;
  `SP_DrawFrame`'s delta). `Com_Frame` doesn't wait for the clock, and the server doesn't pace a
  client's packets on the wall clock (`SV_RateMsec`).
- **Seeds.** InitGame's seed is `regress_seed` (`sv_game.c`, `SP_SpawnServer`, `SP_FinishTransition`).
  libc `rand()` is reseeded from the seed plus the game time before every game frame and ClientThink.
  That covers the SP game, the botlib and engine draws in between.
- **Input.** `CL_CreateCmd` replaces the local player's usercmd with a pattern based on the frame
  number: walk, strafe, turn, jump, crouch, fire.
- **Fingerprint.** Single player: the bridge's `SP_GameRunFrame` (it now wraps every `ge->RunFrame`)
  calls the game's `G_RegressDump` (`efgame/src/game/g_regress.cpp`, found with dlsym). For each
  in-use entity it records the class, origin, angles, health, flags, think, mover, animation, event,
  enemy, the ICARUS task IDs and the NPC behaviour state. For each client it records the player state.
  Holomatch: `SV_RegressFrame` logs every linked entity's `entityState_t` and every client's
  `playerState_t`, with all networked fields from msg.c's tables (`MSG_RegressFields`) except ping.
  A line is written only when an entity changed. Times that just follow the clock are logged
  relative to it.
- **No dialogs.** The runner starts the game with `VOYAGERSP_TEST_NODIALOG=1`. With it, `Sys_Dialog`
  (`sys_osx.m`) prints to stderr instead of showing an alert, as a dedicated server already did. A fatal
  error ends the run with exit code 3, and `check` reports it as FAILED. The runner checks every
  scratch HOME's pak symlinks before it launches. On exit or interrupt, it waits for its game process to
  quit before it deletes the scratch folder.
- `regress_frames` sets the number of frames before it quits. `regress_cmdframe`/`regress_cmd` run a
  command at a given frame (the transition).

**Runtime**: about 55 s for all five scenarios (borg1 21 s, save 8 s, transition 8 s, listen 13 s,
dedicated 2 s), plus about 5 s to assemble the app. Baselines are about 1.6 MB gzipped.

**Limits.** It doesn't check rendering, sound, music, the UI and menus, real input handling, network
timing or the cgame. It also misses game state the fingerprint leaves out: QVM-private Holomatch
data, and SP fields that aren't listed. It covers only what the scenarios reach. The SP and listen
runs open a window: don't touch it, since Esc or the console would pause SP. A game change that adds
rand() calls or moves them shifts everything after that point. That is a real difference, but a large
one. The sp-save baseline needs a voy1 save (`REGRESS_SAVE=<file.sav>` picks one).

Tested: `record` and then `check` on the same build gave identical results in all five scenarios,
with the same hashes on three separate builds. A 1% gravity tweak in `PM_SlideMove` (efgame) was
caught in all three SP scenarios, for example in sp-save at frame 10, player `org` z
572.375 -> 572.36499 and `vel` 185 -> 184. Both Holomatch runs stayed identical, which is correct:
Holomatch runs the retail QVM. Changing `SURFACE_CLIP_EPSILON` from 0.125 to 0.126 (engine) was caught
in all five, at frame 0 or 2. Before `SV_RateMsec` stopped pacing in harness mode, hm-listen varied by
one frame in when the client spawned. A missing pak0 now exits with code 3 and no alert. Not verified:
runs on another Mac or another macOS version.

## SP follow-ups: door state in saves, area sound culling, lip sync, voice lines (2026-11-07)

1.4.0 branch `1_4_0/sp-followups`. Longer notes: `sp-followups.md` in the shared notes folder.

- **Door (areaportal) state in saves.** The save's `PRTS` chunk holds
  the areaportal reference counts, numAreas^2 ints (PC saves confirm it: 6 areas -> 144
  bytes, nonzero symmetric pairs for open doors). The port wrote 1296 zero bytes and skipped it on
  load, so every door read as closed after a load. Now `SP_SaveGameInternal` writes
  `CM_PortalState()` and an eFULL load applies it after the spawn (`CM_SetPortalState`);
  eAUTO loads respawn fresh and ignore it. The save doesn't say which entity
  holds a portal, so `g_entPortalOpen` becomes -1 (unknown) and a close from unknown only releases
  a portal that is held open (never a negative count, which CM ERR_DROPs on). A size that doesn't
  match the map (old port saves on maps that don't have 18 areas) is ignored, as before.
  New CM functions in `cm_test.c`: `CM_PortalStateSize`, `CM_PortalState`, `CM_SetPortalState`.
- **Looping sounds behind closed doors.** `SP_EntInView` (`BuildSnapshot`'s loop culling) adds
  an area test: a loop whose areas are all cut off from the view's area is silent. Any
  connected area keeps it, so nothing that should be heard is dropped. Entities are still all sent (whole
  entity culling was not done: the cgame's events, prediction and the 256 cap would change).
- **Lip sync.** The head skins are -1 blink, -2 frown, -3 both, -4
  neutral, -5..-8 mouth shapes, and `S_Override` is -1 (talking, quiet) or 5..8 from the voice's
  own volume: the mean square of 10 samples 100 apart (22 kHz) at the play position against
  `s_threshold1..4` (0.5, 4, 7, 8) times the sound's loudest sample, redone every 800 samples.
  The port cycled random 1..8 (so talkers blinked and frowned). Now `S_VoiceLipSync`
  (`snd_dma.c`, base mixer; the loader keeps every 100th sample as 8 bit in the sfx) feeds
  `SP_UpdateVisemes` every sim tick and render frame. OpenAL: a 5..8 cycle.
- **Voice lines.** A line the script started right after a cutscene skip ended (same render frame)
  was dropped by the skip's sound block; the block is re-checked per tick now. In single player
  the mixer no longer steals CHAN_VOICE / CHAN_VOICE_ATTEN channels when all are busy.
- **First voice line after quitting from the pause menu (2026-11-08).** The bridge's pause/skip
  gates (`s_spSoundsBlocked`, the skip draw gate, the paused/skipping edge flags) are set per drawn
  frame, so quitting from the in-game menu left sounds blocked into the next game. A New Game's
  level-start frames run before its first drawn frame, and borg1 starts Janeway's first captain's
  log there: it was dropped, its dialog wait ended at once, and the intro began at the second log
  ("Ensign Munro is second in command..."). Seen after skipping the intro, because Esc only opens
  the pause menu once the camera scene is over. `SP_UnloadGame` (new game, load, abort) now resets
  the gates. 1.3.0 had the same bug.

## Holomatch: cMod's game modules, widescreen HUD and lag compensation (2026-11-07)

All of this is from cMod by Noah Metzger (Chomenor). He gave permission to ship his released
game modules (`pakcmod-release-*.pk3`, built under the STEF Game Source License). The app bundles
one in `Resources/cmod/` with its license and a README (`macos/make-app.sh`, `CMOD_PAK`). The pk3
is not in git (`*.pk3` is ignored); put the release in `macos/cmod/` before building the app.

- **VM order.** In Holomatch the game's own paks (pak0-pak3, by checksum) are searched first for
  cgame/ui/qagame, so add-ons like tfgpak or `vm.pk3` no longer replace the stock modules by name
  order. A mod folder's own modules still win, and pure servers still pick the server's paks.
- **Recognition by hash.** cMod's releases are known by checksum (the list cMod's engine uses,
  2021-09-18 to 2025-12-16). The bundled pk3 is loaded privately: it is never in the search path,
  never referenced, offered for download or listed in `sv_paks`, so stock clients are not pushed it.
- **Servers** (`sv_cmodGame`, default 1): dedicated servers and Host a Server run cMod's qagame.
  Config... > cMod sets Run cMod's game code, Lag compensation (`g_unlagged`: Off, Instant-hit
  weapons (default), All weapons), Smooth other players' movement (`g_smoothClients` 1) and Let
  cMod players see their own shots at once (`g_unlaggedPredict` 1). Test hook tab name: `cmod`.
- **Clients** (`cl_cmodModules`, default 1; launcher "Use cMod's game modules, by Noah Metzger"):
  on non-pure servers the client runs cMod's cgame and ui. On pure servers the server's modules
  run, as before. A mod's own cgame/ui is replaced only when the server's `!modcfg` configstring
  asks for cMod's (`nativeCgame`/`nativeUI` 2).
- **Widescreen.** Our 640x480 flexible-display layer is off for cMod's modules only; they draw
  with the real screen size. `cg_aspectCorrect` defaults to 1 in Holomatch (as in cMod's engine).
  The Holomatch video-mode list (HMVideo) is off for cMod's ui. Launcher HUD... sheet: Widescreen
  (`cg_aspectCorrect`), HUD position Edges to Middle (`cg_aspectCorrectCenterHud`, written only
  when moved) and weapon prediction (`cg_predictWeapons`). Stock modules look exactly as before.
- **GetValue.** cMod's modules ask the engine which options it has through a call whose number
  is in the cvar `//trap_GetValue` (700). `CL_CmodGetValue` answers only cMod's modules and only
  for what this engine has: our version string, the brightness settings the renderer already
  ported from cMod, `r_swapInterval`, anisotropy, multisample (`r_ext_multisample`), JPEG
  screenshots, OpenAL, and "skip" keys for settings this engine lacks (`r_glDriver`, color/depth/
  stencil bits, `r_lowEndVideo`, `s_khz`, `k_language`, A3D). Renderer choice, framebuffer, raw
  mouse and windowed-only `r_mode` stay unanswered.

Regression harness: Holomatch scenarios set `sv_cmodGame 0` and `cl_cmodModules 0`, so the
stock path stays recorded and comparable; `REGRESS_CMOD=1` keeps cMod's modules on. With cMod's
qagame the listen and dedicated scenarios differ from frame 0, as expected (different game code).

Known, unchanged: a pure server references tfgpak for cgame/ui by name order, so a client can
download tfgpak and run its cgame. Pure servers stay as they were.

### Fixes after the first 1.4 test build (2026-11-08)

- **Listen servers (Solo Match, Create Server, the game's own map menus).** On a listen server the
  client's `//trap_GetValue` cvar is visible to the server's qagame too, so cMod's qagame made the
  call and the server stopped with "Bad game system trap: 700". `SV_GameSystemCalls` now answers it
  with "unknown key" (cMod's game code then does without those extras). Dedicated servers never had
  the cvar.
- **Holomatch text tables.** `ext_data/` files (the numbered menu/HUD texts, `mp_*text.dat`) are
  now looked up in the same order as the game modules: the game's own paks before other baseEF
  pk3s. Before, a stock ui from pak2 read tfgpak.pk3's tables, whose extra lines overflow the
  stock ui's text arrays: the main menu showed a stray "...WINNER OF A ROUND IN WEAPONRY..." line.
  Pure servers keep the server's order (and give tfgpak's ui its own tables, as before).
- **Widescreen HUD default.** cMod's modules register `cg_aspectCorrect` with 0 (his engine sets 1
  first), so a cMod cgame that ran on an older build or a mod server saved 0 in that folder's
  `hmconfig.cfg` (seen in the mod folders of a real install). The launcher no longer reads the HUD
  choice from `hmconfig.cfg`: it keeps its own (`HudWidescreen` in its defaults, widescreen until
  chosen otherwise in the HUD... sheet) and passes it on the command line, and the engine re-applies
  that command-line value when a cgame or ui starts, so a mod folder's config can't undo it.

### cMod column in the Holomatch server browser (2026-11-08)

Browse... has a **cMod** column: servers running cMod's engine (by Noah Metzger) show its version
("2.0", "1.29", or a check mark when it doesn't say), with the full version string as the cell's
tooltip; the column sorts like the others. `getinfo` answers carry nothing that tells cMod apart,
so the browser now sends each server that answered its `getinfo` one `getstatus` as well (same
socket, up to a second more for the answers). cMod's engine puts `version_cmod` there and calls
itself "cMod HM v1.20 compatible" (or "ioEF:cMod HM ...") in `version`. `g_unlagged` isn't used:
UAM and Razor servers on other engines set it too. The status answers also fill the players list,
so selecting a server shows its players without asking again (the refresh button still asks).

A server hosted with this app shows no cMod mark even when it runs cMod's qagame: `version_cmod`
comes from cMod's engine, not its game module, and the game module adds no key of its own. Ours
reports `version` "Elite Force for Mac ... HM v1.20 compatible" and `version_engine`.

## SP: ambient and mover sound sets load again (2026-11-10)

`sound/sound.txt` (pak0) defines 103 sound sets: 48 `generalSet` (a map area's background loop and
random one-shots, chosen by worldspawn's and triggers' `soundSet`), 36 `localSet` (an entity's own hum
and one-shots: target_speakers, consoles, force fields) and 19 `bmodelSet` (start, moving and stop
sounds of doors, plats and lifts). The SP bridge's reader expected each set's keywords in a `{ }`
block, but the file is line based: a set line, then its keyword lines up to the next set, with `;`
comment lines and a short tool header. So no set loaded, and every door, lift and room ambience that
uses one was silent (borg1's lifts, the scav maps' trek doors, the Dreadnought's engine hum and hull
creaks, Voyager's bridge, the Borg regeneration alcoves).

`SP_AS_ParseSets` (sp/sp_bridge.cpp) now reads the line form (a `{ }` block is still accepted).
Other changes there:
- The kinds are looked up separately: `dntractor` is both a localSet and a bmodelSet (dn5), and maps
  put bmodel set names on target_speakers (borg1's `largeelevator`) that must not start playing
  the lift's sounds at random.
- Up to 16 one-shots per set (`messhall` has 13).
- Sounds are registered for the sets the map names (the cgame's precache list, which the trap used
  to ignore) and on first use for the rest, instead of every sound of every set.
- A local set's loop and one-shots follow the same out-of-view test as entity loops
  (`SP_EntInView`): silent behind a closed door.
- A local set's first one-shot waits a random interval instead of all consoles beeping at once
  when they come into view, and a time left from another run (a loaded save) is rescheduled.

Regression harness: Holomatch and sp-borg1, sp-transition identical. sp-save differs only in
`event` of voy1's func_door 127 (voyagerdoors) from frame 361: the door now sends its
EV_BMODEL_SOUND start and stop events. No other field changes.

## Cmd+W and the close button ask before quitting (2026-11-11)

Closing the game window (its close button, or Cmd+W, the Window menu's Close) used to quit at
once: SDL turns the last window's close request into SDL_QUIT, which runs `quit Closed window`.
`IN_Init` now sets `SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE` to 0, so a close comes only as
SDL_WINDOWEVENT_CLOSE, and `Sys_MacConfirmQuit` (sys_osx_data.m) shows a "Quit Elite Force?"
sheet on the window (the app's display name; Quit on Return, Cancel on Escape). Cmd+Q and the
app menu's Quit still send SDL_QUIT and quit at once, as before.

- Single player waits for the answer (game stopped, sound paused); Holomatch keeps running.
- The pointer is released while the sheet is up and captured again after Cancel; the sheet has
  the keyboard, and the game window is made key again after Cancel.
- Works in fullscreen (the sheet sits on the fullscreen window). Dedicated servers (no window)
  and the launcher window are unchanged. With `VOYAGERSP_TEST_NODIALOG` a close quits at once.
- Test hooks: `VOYAGERSP_TEST_QUIT_ANSWER=quit|cancel`, `VOYAGERSP_TEST_QUIT_PRESS=<s>:<what>,...`
  (front, close, cmdw, cmdq, key), see the top of sys_osx_data.m.

Regression harness: all five scenarios identical.

## Quiet background mode for automated test runs (2026-11-11)

`VOYAGERSP_TEST_BACKGROUND=1` (any value but empty or `0`) keeps a test run from taking the user's
focus, keyboard or mouse, so tests can run while someone works. It covers the launcher and the game
(single player and Holomatch); a dedicated server has no window anyway. Without it nothing changes:
every new branch is behind the variable. Details at the top of `sys/sys_osx.m`.

- **No activation.** The app becomes an accessory app (`NSApplicationActivationPolicyAccessory`: no
  Dock icon, so no Dock bounce or attention request). SDL's `SDL_MAC_BACKGROUND_APP` and
  `SDL_WINDOW_ACTIVATE_WHEN_SHOWN` / `_RAISED` hints are set in `main()` (in the environment and as
  overrides). Every `activateIgnoringOtherApps` in the launcher, data and server code goes through
  `VSP_ActivateApp`, which does nothing in this mode.
- **Windows behind.** `VSP_ShowWindow` / `Sys_MacTestBackgroundWindow` put a window one level below
  normal windows (`NSNormalWindowLevel - 1`), behind every other app's windows, without making it
  key. Plain `orderBack:` wasn't enough: the window server kept a just-shown or modal window of the
  inactive app in front, and `runModalForWindow` lifts the launcher to the modal panel level, so
  `VSP_KeepBehindWhenModal` puts it back once the modal loop runs. The game window is placed the same
  way when it's created (`VKimp_Init`, and `VKimp_ReplaceWindow` instead of `SDL_RaiseWindow`). The
  windows are still drawn: `screencapture -l <window id>` captures them.
- **No pointer capture.** The game is always windowed (`VKimp_DesiredWindow` ignores `r_fullscreen`,
  without changing the cvar), and `IN_Frame` never captures the mouse or hides the cursor.
- **No stray input.** Keyboard, text, mouse, wheel, joystick/controller and touch events from the
  system are dropped in `IN_ProcessEvents`, and the controller isn't polled. Console commands on
  stdin, `+exec` and the other `VOYAGERSP_TEST_*` hooks still work (`VOYAGERSP_TEST_QUIT_PRESS`'s
  `key` step is a synthetic key event, so it is dropped too).
- **Full rate.** Focus loss and minimizing don't set `com_unfocused` / `com_minimized` (the game
  runs as if focused, including `com_maxfpsUnfocused` and `s_muteWhenUnfocused`), and App Nap is held
  off with an `NSProcessInfo` activity.
- **No alerts.** `Sys_Dialog` prints and takes the default, as with `VOYAGERSP_TEST_NODIALOG`.
- `macos/regress.sh` sets it by default; `VOYAGERSP_TEST_BACKGROUND=0` shows the window the usual way.

Checked with a test bundle and a scratch HOME while another app was frontmost: the frontmost app
stayed frontmost through the launcher (autoplay), single player and Holomatch; the windows sat behind
all other windows (layer -1); `in_mousestate` showed no grab or relative mode; about 91 frames per
5 s at `com_maxfps 85` while unfocused; SP level time grew in step with the clock (saves 5 s apart:
t=107000, then 112000), and the Holomatch timer went 0:19 to 0:29 in 10 s; `screencapture -l`
captured the launcher and the game window.

Regression harness (in background mode): all five scenarios identical to a baseline recorded at
2e80964 before the change.
## Co-op M0: scaffolding and the player 2 puppet (2026-11-07, branch `1_4_0/coop` only)

Experimental, never merged without the user's approval. Plan: `docs/coop-proposal.md`, `docs/coop-audit.md`.

- **Build switch.** CMake option `EF_COOP` (default OFF) in `efgame/CMakeLists.txt` and in the engine's
  CMakeLists (the engine side adds nothing yet). With it ON, `efgame/src/coop/*.cpp` is compiled and
  `EF_COOP` is defined for libefgame. `EF_COOP=1 macos/make-app.sh` builds it in `efgame/buildmac-coop`.
- **Run switch.** Latched cvar `sp_coop` (registered only in `EF_COOP` builds, read at InitGame).
- **Helpers** (`efgame/src/game/coop_public.h`): without `EF_COOP` each one is the exact expression it
  replaced (`COOP_IsPlayer(e)` is `((e)->s.number == 0)`), and each hook is nothing.
- **Gate.** `macos/regress.sh check coop-base` (baseline recorded at dbfb6a3 with `EF_COOP` OFF) after
  every batch, both for an OFF build and for `EF_COOP=1 REGRESS_ARGS="+set sp_coop 0"`.
  `REGRESS_ARGS` (new, default empty) adds arguments to every harness run.
- **Player 2 puppet (B1).** `efgame/src/coop/coop_player.cpp`. With `sp_coop 1`, P2 spawns a few frames
  after every level start (new game, load, map change) at a clear spot next to P1 (traces from P1, with
  floor under it). It is a client entity with its own client struct (a static in the co-op module, never
  in `level.clients`), no NPC struct, no ICARUS and no script name, so scripts keep finding only P1
  ("Munro"). It looks like P1 (copied model info) and shares P1's weapons. Input: follow P1 (run toward
  P1, hop when stuck, jump to P1 when stuck 3 s or more than 1024 units away), or the console commands
  `coop_p2`, `coop_p2_follow`, `coop_p2_move`, `coop_p2_turn`, `coop_p2_fire`, `coop_p2_use`,
  `coop_p2_weapon`, `coop_p2_tele`, `coop_p2_damage`. They are registered by the cgame (only with co-op
  on) and reach the game through `ClientCommand`, because in SP the game's `ConsoleCommand` is only
  reached while the HM server runs.
- **Death.** `player_die` hands P2 to `COOP_P2Die` (no game over, no targets); a dead P2 never takes the
  player respawn path (`load *respawn`), and comes back next to P1 after 4 s.
- **Cameras.** `CGCam_Enable`/`CGCam_Disable` hide P2 (`SVF_NOCLIENT`, not solid, no damage, no target)
  and show it again, back next to P1 if a script moved P1 away. P2 does not move while a camera runs and
  obeys `player_locked`.
- **Saves.** `WriteGEntities` skips P2; `COOP_PreSave` first clears every saved entity pointer to P2
  (all `F_GENTITY` fields of the entity, NPC and client save tables; `activator` becomes P1), look
  targets, alert-event owners and P2's timers. Tested: a save written in co-op loads in an `EF_COOP` OFF
  build with no warnings and without the P2 entity.
- **Weapons (B2).** Weapon and missile code that tested `s.number == 0` for "the player" uses
  `COOP_IsPlayer`, so P2's shots get the player muzzle, spread, damage and impact rules. P2 is always
  first person (`COOP_ThirdPerson`), and the co-op module keeps P2's eye point and view angles current
  (only the cgame updates P1's). Tested: P2 fires on borg2, ammo drops 2 per shot, no warnings.
- **Combat (B3).** `G_Damage`, friendly fire and the mission status text treat P2 as the player (no
  half damage from crew, crew shots by accident do not hurt it, pain scripts). The pointer compare in
  `G_FriendlyFireReaction` uses `COOP_IsPlayerPtr` (a freed entity also has number 0). The skill
  scaling of player damage is commented out in this code, so there is nothing to scale for P2. The
  falling clamp only covers client attackers; world falling damage (attacker NULL) can kill P1 too, and
  does the same to P2. `coop_p2_damage <n> fall` tests it.
- **Movement (B4).** `bg_pmove.cpp` and `bg_panimate.cpp` (also built into the cgame prediction) use
  `COOP_IsPlayer` / `COOP_IsPlayerNum` for footstep alerts, landing, use and crouch rules, and
  `COOP_ViewClientNum()` (0 on the host) where the code means "the client whose view is drawn".
- **Triggers and items (B5).** PLAYERONLY `trigger_multiple`/`trigger_once`/`trigger_push`, item and
  ammo-crystal pickups, assimilation triggers and the stasis shooter accept either player.
  `trigger_entdist` "player within N" measures to the nearest player. `trigger_visible` loops over the
  players (the first one to look fires it, with P1 as activator) and stops after it frees itself.
  Helpers `COOP_NearestPlayer`, `COOP_NumPlayers`, `COOP_PlayerByIndex` count P2 only when it is alive
  and not hidden by a camera. Tested on dn1: P2 placed on a compression rifle with `coop_p2_tele x y z`
  (new) picks it up (ammo back to 300, the item goes to its respawn state).
- **Enemy choice (B6).** Where the AI took "the player" as its enemy (alert events, stealth checks,
  `NPC_PickEnemy` players-first, `NPC_FindPlayer`, `NPC_CheckPlayerDistance`, `AI_DistributeAttack`,
  the Vohrsoth boss, stasis fliers) it now takes the nearest player. Pointer compares use
  `COOP_IsPlayerPtr`. Tested on borg2 (1500 frames): NPCs 88, 197 and 203 switch to P2, P2 dies and
  comes back next to P1; voy3 runs clean.
- **Perception and navigation (B7).** Dead-body removal and shy spawns wait until no player is close or
  sees the spot (a loop over the players, same checks in the same order). PVS tests for the "remove when
  unseen" behavior state and the nav debug drawing accept either player (`COOP_AnyPlayerInPVS`, and a
  `Rev` form where the old call had the points the other way round). Blocking, stacked-canyon and
  goal-is-the-player checks in `g_nav.cpp` include P2, and `NAV_FindPlayerWaypoint` also keeps P2's
  waypoint current, so NPCs chasing P2 can path to it. Tested on borg2 and voy3 (1500 frames).
- **NPC reactions (B8).** Use, greet and "hit by the player" reactions in `NPC_reactions.cpp` accept
  P2 (greetings play Munro's lines, which P2 shares). Tested on the voy1 save: P2 placed next to a crew
  member, facing it, presses use, and the NPC answers (a sound event that a run without the use does
  not have).
- **Shared inventory and carry-over (B9).** `efgame/src/coop/coop_inventory.cpp`. After a pickup in
  `Touch_Item`, weapons and holdables go to the other player too; ammo, health and armor stay with
  whoever touched them first. Item targets and `trigger_multiple` targets fire with P1 as activator
  (`COOP_StoryActivator`), so scripts still see "the player". A script `set weapon` on P1 gives P2 the
  weapon with full ammo and switches it. `G_ChangeMap` stores P2's health, armor, weapon and ammo in
  the cvar `coop_p2save` (like P1's `playersave`: the game library is reloaded for each map); the
  next level's InitGame reads and clears it, and P2's first spawn applies it. Borg adaptation stays the
  single counter on P1 (P2's hits already count there), and `player_locked` already freezes P2 (B1),
  so neither needed a hook. Tested on dn1: P2 picks up the tricorder and both players get it; a
  `coop_p2save` value carries into borg2 (health 55, armor 20, ammo 150).
- **Launcher: Co-op… (2026-11-08).** `sys/sys_osx_coop.m` (new, compiled only with `EF_COOP`, which the
  engine CMakeLists now also defines for `efsp`). The Single Player card gets a "Co-op…" button (non-co-op
  builds have no button and the same launcher code). Its sheet has **Host**, which plays single player
  exactly as the card's Play does, plus `+set sp_coop 1`, and lists this Mac's IPv4 addresses (en0 first)
  for the friend; and **Join**: an address field (`host` or `host:port`) and **Check**, which shows what a
  host answers (mission and map, difficulty, minutes on the level, players, the host's app and co-op
  versions), "No co-op game found" after 2 s without an answer, or a version mismatch (another co-op
  protocol or app version). **Connect** is shown but disabled until M1 ("Joining a co-op game comes with
  the next co-op update (M1)").
  - **Port and protocol.** UDP **29760** (`VSP_COOP_PORT`): clear of Holomatch's 27960, the test ports
    27980-27983 and Jedi Academy's 29070. In SP the engine's networking is off (`net_enabled` 0), so a host
    with `sp_coop 1` opens its own small socket there, polled once a frame from `Com_Frame` (an `EF_COOP`
    line), and closes it when `sp_coop` is 0. Query `\xff\xff\xff\xffgetcoopinfo [challenge]`, answer
    `\xff\xff\xff\xffcoopInfoResponse\n` and an infostring: `coopProtocol` (`VSP_COOP_PROTOCOL`, 1),
    `version`, `hostname` (the Mac's name), `state` (playing or menu), `map` (`SP_CurMap`), `mission`,
    `skill` (`g_spskill`), `players`, `maxPlayers`, `time` and the challenge (at most 32 letters, digits,
    `-` or `_`; nothing else of a query is sent back). Rate limited like the server's getinfo (10 a second
    per address, 10 per 100 ms in all); anything else on the port is ignored. The game side publishes
    `coop_mission` (the level title, as the loading screen shows it), `coop_players` and `coop_time` about
    once a second (`efgame/src/coop/coop_info.cpp`). `sp_bridge.cpp` is not touched.
  - **Permissions.** The first Check of a LAN address (and a host's first answer) may bring up macOS's
    "find devices on your local network" prompt. If sending fails (as it can when that was denied), Check
    says so and points to System Settings > Privacy & Security > Local Network. With the macOS firewall on, the host may also be asked
    to accept incoming connections. (Neither prompt came up in testing: host and checker ran on the same Mac.)
  - **Test hooks** (top of `sys_osx_coop.m`): `VOYAGERSP_TEST_COOP_SHEET=<png>`, `..._QUERY=<address>`,
    `..._HOLD=<s>`, `..._HOST=1`.
  - **Tested.** Regression harness against `coop-launcher-base` (8443f66, `EF_COOP` OFF): OFF build and
    `EF_COOP` ON with `+set sp_coop 0` ALL IDENTICAL; the OFF build's launcher object code is the same as
    before. Host pressed through the test hook on voy1: answers on 127.0.0.1 and the LAN address (players 2
    once P2 spawned), ignores other packets, never echoes junk after the query, answers 10 of 30 queries sent at once. Join's check (run in
    a second instance): found, no answer, version mismatch (a scripted fake host), and bad addresses.
  - **For M1.** The query is a status probe only. The co-op protocol replaces the stand-in P2 count, adds
    the `.pk3` checksum check to the version check, and probably moves the answer onto the co-op netchan
    socket on the same port. Connect, Copy Invite and router port opening come then.
## Co-op M1: the connection and the guest steering player 2 (2026-11-09, branch `1_4_0/coop-m1` only)
- `efcode/sys/sys_coop_net.c` (EF_COOP only): a small UDP protocol on the co-op port 29760, sharing
  the host's check socket. Handshake (`coopConnect` / `coopAccept` / `coopReject`): app version, co-op
  protocol (now 2), a signature of the loaded `.pk3` files, optional password (`coop_password`, or the
  launcher's field, never on the command line), one guest, 10 s timeouts, disconnect messages both ways.
  In-session: the guest's newest commands (4 per packet), keepalives, the host's status once a second.
  Console: `coop_connect <address[:port]>` (cvar), `coop_disconnect`, `coop_kick`, `coop_status`.
- The check's answer adds `needPassword` and `guest`. The launcher's Join enables Connect after a Check
  finds a game.
- The guest's input reaches the game through `COOP_SetBridge` (`game/coop_bridge.h`, exported from
  EF_COOP game modules only, set by `sp_bridge.cpp` after each module load) and steers P2 in place of the
  follow-P1 input; with no guest, or input older than 0.5 s, P2 follows P1 as before.
- Until the replica guest (`docs/coop-m1-design.md`), a guest loads the host's map on its own
  (`coop_guestLoadMap 1`): it sees its own copy of the level.
- Tested with two copies on one Mac (127.0.0.1, regress-mode scripted input on both): join, P2 steered
  (drifts away from P1 instead of following), leave, wrong password, version and file mismatch refusals,
  host kick, host level reloads with the guest connected. Gates vs a fresh baseline `coop-m1-base`
  (672ed3c): EF_COOP OFF ALL IDENTICAL (and `efsp` byte-identical to 672ed3c's), EF_COOP ON +
  `sp_coop 0` ALL IDENTICAL.

## Co-op M1: P2 holds still, the pack match, the replica guest (2026-11-10, branch `1_4_0/coop-m1` only)
- While a guest is connected, missing or stale guest input makes P2 hold still; P2 follows P1 only with
  no guest (`COOP_BRIDGE_VERSION` 2 adds `GuestConnected`).
- The pack signature covers only the `.pk3` files single player reads (`FS_CoopPakChecksums`): the core
  packs, packs with `ext_data/` or `.ibi` files, and packs with an SP map a core pack also has. Holomatch
  map packs no longer refuse a guest.
- The replica guest (protocol 3): the host sends its world every game frame as slots (entities as the
  save's chunk images, configstrings, globals, recent server commands), only those changed since the
  guest's ack, compressed with the system's Compression library (linked only with EF_COOP) and cut into
  1200-byte fragments. The guest loads the host's map with `sp_coop 2` and its level shows the host's
  world: no game frames, one snapshot per host frame, P2's player state. `coop_netstats 1` prints bytes/s
  and per-frame times every 5 s; `coop_status` prints them once. `coop_guestLoadMap` is gone. Details and
  where it differs from the plan: `docs/coop-m1-design.md`.
- `macos/make-app.sh` takes the engine build folder from `ENGINE_BUILD` (default `build-macos`), so a
  co-op test app can be bundled from a separate build.
- Prediction (protocol 4): the guest makes one command per host frame it takes in, the host applies one a
  frame in order and names the last in each frame; the guest's cgame predicts from there (it does even at
  timescale 1 in a replica). The view client in a replica is P2 (`COOP_ViewClientNum`).
- Direct sounds (script voice lines, `G_SoundOnEnt`) go with the replica's globals; server commands and
  configstrings already did.
- Measured with two copies on one Mac: 7 to 27 KB/s from host to guest (quiet map to borg2), about 1 ms per
  frame on the host; a Holomatch client with 9 bots gets 1.6 to 2.8 KB/s. Table in `docs/coop-m1-design.md`.

### Co-op M1: door sounds, prints, host pause (2026-11-10)
- Door and lift sound events name their sound set's stage for the guest (`RBMS` chunk), which plays
  its own handle for it; the two copies' handles differ.
- Server commands carry their target: story text goes to every client (`COOP_EVERYONE`), P1's own
  messages stay on the host, P2's go to the guest only.
- The host's status says when its game is paused; the guest shows "Host paused" (or "Waiting for the
  host" when frames stop otherwise), holds P2's input and the game's sounds, and goes on with the
  next frame.

## Co-op M2: cutscenes, level starts, saves (2026-11-10, branch `1_4_0/coop-m1` only)
- The host's cutscene camera reaches the guest as state with the replica's globals (`RCAM`): the
  guest's cgame draws the same shot; bars are scaled to its view; script text colours go along. The
  host's skip is mirrored (skip screen on the guest). A camera track's end fires its targets on the host
  only. Player 2 rejoins player 1 again half a second after a camera ends.
- In-game movies play on the guest too and stop when the host's game goes on.
- A level that starts with a guest connected waits (up to 30 s, "Waiting for player 2") until the guest
  has loaded it; the guest then shows that level's story text from its start (co-op protocol 5).
- Every co-op save writes player 2's state to `<save>.coop` next to the `.sav` (still a plain SP save);
  a load reads it back.
- The guest's view weapon no longer reads past `cgs.clientinfo` ("Bad animFileIndex").
- Details: `docs/coop-m2-design.md`.

### Co-op M2: shots over the internet, router port, invite, bandwidth (2026-11-10)
- Lag compensation (host, `coop/coop_lagcomp.cpp`): player 2's instant-hit shots are traced against the
  clients and moving brush models where the guest saw them (the frame it aimed in, sent with each
  command; co-op protocol 6), at most 400 ms back, everything restored after the trace. The idea is
  cMod's ping compensation by Noah Metzger (Chomenor), used with his permission; the code is ours.
- Shot prediction (guest, `coop/coop_predict.cpp`): the compression rifle's and I-MOD's beam and
  impact show at once; the host's effects of those shots are left out. Player 2's movement events now
  run once on the guest (they ran up to three times). After cMod's weapon prediction, same credit.
- Co-op… sheet: "Open the port on my router" (NAT-PMP / UPnP, as the server window), the public
  address or why not, and Copy Invite (`stvef://coop/<address>:<port>`), which the launcher's link
  handler opens as the Co-op… sheet with the address checked.
- Bandwidth host to guest: borg2 23.8 to 4.3 KB/s, borg1 15.3 to 3.9, voy1 12.5 to 3.0 (host-only
  fields masked from entity images, far entities sent every 10th frame).
- Test cvars: `coop_simlatency` / `coop_simloss` (simulated link), `coop_netstats 2` (which fields
  change), `coop_p2_target` (a swaying target in front of player 2).

## Co-op: enemies and both players, downed and revive (2026-11-11, branch `1_4_0/coop-m1` only)
- Enemies take player 2 as well: a script naming the player as an NPC's enemy gets the player that
  provoked it (or the nearer one), and an NPC fighting one player turns on the other when that one is
  clearly the better target.
- A player whose health runs out goes down; the partner revives them by holding Use (`coop_revivetime`,
  3 s); game over for both when both are down, on bleed-out (`coop_bleedout`, 30 s) or when player 1
  dies for real. No saving while someone is down.
- Details: `docs/coop-m2-design.md`.

## Co-op: guest closed on ladders (2026-11-12, branch `1_4_0/coop-m1` only)
- The guest's game quit (crash, no report) as soon as player 2 was on a ladder or waist-deep in water:
  its cgame predicts player 2's moves with no game entity, and the water/ladder depth check read that
  entity's NPC type (null). The check now treats "no entity" as not a biohulk, as the host does for
  player 2. Since the merge of the single-player fix below, this is one unconditional guard (no
  `EF_COOP` switch).

## Co-op: the guest sees breakables explode (2026-11-12, branch `1_4_0/coop-m1` only)
- Some game code draws its effect by calling the cgame directly (one module in single player), with
  no entity event: a breakable's fireball and debris (plasma filters, conduits, grates, func_breakable),
  turrets' debris and lasers, the Borg head bot's explosion, stasis flier children, the psycho bolt, the
  spawn shimmer. The guest's replica never saw them. In co-op builds `game/coop_fx.h` routes those calls
  through `coop/coop_fx.cpp`: drawn as before, and on a host kept in a short ring that goes with the
  level's globals (like the direct sounds); the guest draws the new ones. Sounds already came through
  entity events.
- Not forwarded: the stasis fliers' and fixits' client think functions (cgame per-frame thinks on
  entities), debug drawing (CG_Cube/CG_Line).
- Test hook: `coop_p2_damage <n> near` (player 2 damages the nearest breakable).

## SP: no crash on a ladder or in water while the player is predicted (2026-11-12)

With `timescale` below 1 the SP cgame predicts the player itself (`CG_PredictPlayerState`) with no
game entity (`cg_pmove.gent = NULL`). `PM_SetWaterLevel` then read `pm->gent->NPC_type` (the biohulk
check) as soon as the player was waist-deep on a ladder or in water, and the game closed. A missing
entity (or NPC type) now counts as "not a biohulk", so the result is the same wherever it didn't
crash before. Reproduced on borg1 (`timescale 0.5`, `setviewpos 371 571 100 0` into the ladder):
SIGNAL 11 in `PM_SetWaterLevel` before, climbs normally after. Regression harness: all five
scenarios identical to a baseline recorded at 4dd6eac.

## Release configuration: co-op on the Mac only (2026-11-12)
1.4.0 ships an experimental co-op mode on macOS and experimental Windows/Linux builds without it.
- `macos/make-app.sh` (and so `macos/make-release.sh`) builds with `EF_COOP` on by default, in
  `build-macos-coop` + `efgame/buildmac-coop`, so `build-macos` / `efgame/buildmac` stay the builds
  without co-op. `EF_COOP=0` builds the app without it. The script stops if the engine and the game
  modules it would bundle disagree about co-op (e.g. `NO_BUILD=1` with the wrong folders).
- `macos/regress.sh` assembles its app without co-op unless `EF_COOP=1` (unchanged gates).
- The CMake option `EF_COOP` now defaults to OFF everywhere (it was ON for Windows/Linux). The
  launcher core only shows the Co-op button when the engine has co-op (`LC_COOP_BUILT`, set by
  CMake with `EF_COOP`); `EF_SHOW_COOP=1` still shows it for testing in an `EF_COOP=ON` build.
- The Mac Co-op sheet already says EXPERIMENTAL and the button's tooltip says "(experimental)".
- Also: the Mac, Windows and Linux packaging read the FFmpeg version from `deps/versions.sh`
  (where the pins moved); the Mac app build stopped at FFmpeg's license without it.
- Gates: `regress.sh check base-1_4_0` ALL IDENTICAL; `EF_COOP=1 REGRESS_ARGS="+set sp_coop 0"
  regress.sh check coop-m2-base` ALL IDENTICAL. Windows cross-build (mingw) with the new default
  (EF_COOP OFF) and with `-DEF_COOP=ON` both compile.

## Renderer: one-shot animations hold their last frame (`RF_CAP_FRAMES`, 2026-11-12)
Elite Force's game code marks one-shot model animations with renderfx `RF_CAP_FRAMES` (0x0400):
explosions (`CG_AddExplosion` in `cg_localents.cpp`) and `EF_ANIM_ONCE` entities (`cg_ents.cpp`);
the Holomatch game code (cMod and stock) sets it too. The engine renderer never knew the flag. An
explosion asks for frame `f + 1`, which reaches `numFrames` in the last part of its life, and
`R_AddMD3Surfaces` turns any frame out of range into frame 0, so the model jumped back to its first
frame just before it disappeared. `R_AddMD3Surfaces`
now clamps `frame` / `oldframe` to the model's last frame when the flag is set, before that range
check. The flag is defined in `renderercommon/tr_types.h` (ELITEFORCE only); nothing else changes.
- Seen on borg2 with the compression rifle in regress mode (fixed 50 ms frames, a `screenshot`
  per few frames): the frame where the white shell is in its last frames shows it fading on the
  wall after the fix, and gone (back to its first frame) before.
- Gates: `regress.sh check base-1_4_0` ALL IDENTICAL; `EF_COOP=1 REGRESS_ARGS="+set sp_coop 0"
  regress.sh check coop-m2-base` ALL IDENTICAL (the harness hashes game state, not pixels).
- The issue had been filed as a shader `animMap` problem; the shader animations were fine, it was
  the model frames.
