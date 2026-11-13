# Windows / Linux launchers: architecture and work split

Goal: Windows (Win32) and Linux (Qt 6) launchers at **100% feature parity** with the
macOS launcher, as specified in `launcher-data-coop.md` ("LDC": main launcher, game
data, co-op, in-game File menu equivalents, updates, links) and `servers.md` (server
browser, masters, hosting, server config). Both share one portable C core.

## Hard rules

1. **The macOS build must not change.** Do not edit any file the Mac build compiles
   (`sys_osx*.m`, `sys_osx_gog.c`, `sys_coop_net.c`, `sys_main.c`, `common.c`, …) and
   do not change the Apple branch of `EFAndroid-SP/app/jni/engine/CMakeLists.txt`.
   New code goes into new files compiled only on Windows/Linux. If a shared file
   truly must change, guard the change so the Apple preprocessor output is
   byte-identical, and say so in the commit message. The core is written so the
   Mac launcher *could* adopt it later; nobody does that now.
2. **Co-op**: write all of it (core + engine side + launcher UI sheet), but the
   Co-op… button is hidden on Windows/Linux by default (`LC_COOP_UI_ENABLED` 0 in
   `lc_common.h`; env `EF_SHOW_COOP=1` shows it, for testing). Since 2026-11-12 the
   Windows/Linux builds default to `EF_COOP` off (co-op is a Mac-only experiment in
   1.4.0), and without `EF_COOP` the button never shows, env or not (`LC_COOP_BUILT`).
   To try co-op there: configure with `-DEF_COOP=ON`, then `EF_SHOW_COOP=1`.
3. Strings: use the exact user-visible texts of the specs. Replace "Mac" / "this Mac"
   with `LC_PLATFORM_NOUN` ("PC" on Windows, "Computer"/"computer" on Linux: "Play on
   This PC", "Play on This Computer"), "Finder" with "File Explorer" / "the file
   manager", "Trash" with "Recycle Bin" on Windows, Cmd shortcuts with Ctrl.
4. Never `git push`. Commit on your own branch. Commit messages end with
   `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Process model

### Windows (`EliteForce.exe`, one executable)
- The launcher runs **in the game process** before `Com_Init`, like macOS:
  `Sys_PlatformLauncher` → `Sys_WinLauncher(argc, argv, commandLine, size)` (Win32 UI,
  `efcode/sys/win32/`), which appends the `+set` arguments (`lc_launch_append_args`).
- `EliteForce.exe --host-server` → only the server window, then exit.
- Quit to launcher / Switch mode / Restart: at exit, spawn `EliteForce.exe` (no args =
  launcher, or `+set com_holomatch N`) detached (`lc_spawn_detached`).
- Common Controls v6 (manifest already present), Per-Monitor-V2 DPI, dark mode where
  Windows allows (title bar via DWM, follow `AppsUseLightTheme`).

### Linux (AppImage: `efsp` + `ef-launcher`)
- `ef-launcher` is a separate Qt 6 Widgets executable (`efcode/sys/qt/`), so `efsp`
  never links Qt. AppRun: no args → `ef-launcher`; `--sp/--holomatch/--server` stay
  as today (straight to efsp); `--host-server` → `ef-launcher --host-server`;
  a `stvef://…` argument → `ef-launcher <link>`.
- Play: `ef-launcher` builds the argument vector with the same core code
  (`lc_launch_build_argv`) and **execv**s `efsp` with env `EF_FROM_LAUNCHER=1`
  (so efsp knows a bare `quit` returns to the launcher).
- efsp started with no mode argument and without `EF_FROM_LAUNCHER`/`EF_NO_LAUNCHER`
  execs a sibling `ef-launcher` if there is one (so running efsp directly also shows
  the launcher); otherwise it behaves as today.
- Relaunch / new instances from inside an AppImage must go through `$APPIMAGE`
  (the mount disappears when the first process exits): `lc_self_command()` returns
  `$APPIMAGE` when set, else the executable path; AppRun forwards the arguments.
- Steam Deck / gamescope (`SteamDeck=1` or `GAMESCOPE_WAYLAND_DISPLAY` set): larger
  controls, full keyboard/D-pad focus navigation, and a "Start the last mode
  automatically" option (store key `autolaunch`; `--launcher` forces the launcher).
  This is the Deck addition on top of parity.

### Engine side shared by both (new `efcode/sys/sys_platform_desktop.c`, replaces
`sys_platform_stub.c` on Windows/Linux)
- Hooks of `sys_platform.h`: homepath (as the stub), launcher dispatch (Windows UI
  call / Linux exec of ef-launcher), game data (`Sys_DataResolve`), quit-to-launcher
  and relaunch-at-exit (LDC §5.1), the File menu equivalents as console commands
  `launcher`, `switchmode`, `showsaves`, `showgamefolder`, `changegamefolder`
  (relaunches at the launcher with the chooser open: `--choose-game-folder`),
  `deletesettings` (LDC §5.4; confirmation via `SDL_ShowMessageBox`; Recycle Bin /
  Trash via `lc_move_to_trash`). The in-game close-window confirmation (§5.3) only
  if it can be done without changing what the Mac build compiles; else leave it out
  and say so.
- Co-op engine side for Windows/Linux: new `efcode/sys/sys_coop_host.c` = portable
  port of the engine part of `sys_osx_coop.m` (LDC §6.3). `sys_coop_net.c` compiles
  unchanged with a forced-include compat header (`sys_coop_compat.h`: zlib raw
  deflate for `compression_encode_buffer`/`compression_decode_buffer` with
  `COMPRESSION_ZLIB`, `arc4random*` → portable random, Winsock mapping). zlib's
  deflate sources are added to the non-Apple build only.

## The core: `EFAndroid-SP/app/jni/efcode/launcher/`

Static library `eflauncher` (C99, no SDL, no engine headers, no Z_Malloc/Com_Printf),
CMake target defined in `efcode/launcher/CMakeLists.txt`, included by the engine
CMake only when `NOT APPLE`. Linked into `efsp` (both platforms: relaunch, data,
delete; Windows: the in-process UI too) and `ef-launcher` (Linux). Unit tests in
`tests/launcher/` (plain C; build and run on the Mac host too — the core must
compile on macOS for that, it just isn't used by the Mac app).

| Header | Owner (branch) | Covers |
|---|---|---|
| `lc_common.h` | base (done) | paths, UTF-8 file I/O, dirs, threads, mutex, time, process spawn, open URL/folder, trash, logging, settings store, HTTP |
| `lc_settings.h`, `lc_launch.h`, `lc_data.h`, `lc_update.h`, `lc_links.h` | core-a | LDC §1–5, 7, 8, 10.1–10.8 |
| `lc_server.h`, `lc_net.h`, `lc_host.h`, `lc_nat.h`, `lc_coop.h` | core-b | servers.md (all); LDC §6, 10.9 |

The headers are a contract. The owner may **add** declarations and fill in details;
an existing signature changes only if it's unimplementable, with a note in the
commit message. UI agents never edit core headers; if something's missing they
write a local helper in their UI code and list it in their final report.

Async: core functions are blocking; the UI runs them on its own worker thread
(`lc_thread_start`) and marshals results to the UI thread (Win32 `PostMessage`,
Qt queued signals). Callbacks are called on the thread that called the core
function — except the explicitly asynchronous APIs (`lc_browser_fetch`, `lc_nat_*`,
`lc_proc_*`, `lc_coop_*` watchers), whose callbacks come from a core worker thread;
the UI must marshal them. Cancellation is a `volatile int *cancel` the core polls.

HTTP (update check, efservers.com 3D lookup): libcurl loaded at run time
(`lc_http_get`): Windows `libcurl-4.dll` (bundled), Linux `libcurl.so.4` (system).
No curl → `LC_HTTP_UNAVAILABLE`, the feature degrades quietly.

NAT: NAT-PMP (RFC 6886) + UPnP IGD (SSDP + SOAP AddPortMapping / DeletePortMapping /
GetExternalIPAddress), hand-written in `lc_nat.c`, no new library. Gateway: Windows
`GetBestRoute`, Linux `/proc/net/route`.

Settings store: `<homepath>/launcher.cfg`, lines `key "value"`, last wins, unknown
keys kept on rewrite, atomic save. `gamefolder` stays compatible with `sys_data.c`.
Lists use indexed keys (`masters.n`, `masters.0`, `masters.1`, …).

## Branches

All off `1_4_0/xplat-launcher`: `1_4_0/xplat-launcher-core-a`, `…-core-b`, `…-win32`,
`…-qt`. UI branches merge the core branches (`git merge`) whenever they need newer
core code; core branches never merge UI branches. The integrator merges all four
into `1_4_0/xplat-launcher`.

## Builds

- macOS: `macos/make-app.sh` / the macOS preset must build exactly as before.
- Windows: `windows/make-release.sh` (mingw-w64), test under the CrossOver bottle
  `ef-xplat-test` (`~/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine
  --bottle ef-xplat-test …`). Never kill processes you didn't start (the user may be
  playing).
- Linux: Docker sniper SDK image `registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest`,
  mount the worktree at its own path (`-v "$PWD:$PWD" -w "$PWD"`); Qt 6.8.3 prefix
  `build-deps/linux/qt` (from `linux/build-qt.sh`); `linux/make-appimage.sh`.
