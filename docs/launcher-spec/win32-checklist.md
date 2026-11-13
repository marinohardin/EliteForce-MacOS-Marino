# Win32 launcher: parity checklist

Code: `EFAndroid-SP/app/jni/efcode/sys/win32/wl_*.cpp` (entry `Sys_WinLauncher` in `wl_main.cpp`).
Status: done = implemented as specified; DEV = deviates (reason); TODO = not done.

## LDC (launcher-data-coop.md)

| Item | Status |
|---|---|
| 2.1 window, centered, Quit on close, dark/light, group style | done (title bar is the system one; height scrolls when the work area is too small) |
| 2.2 header, update badge, Download… | done; gear "…" menu (About, Check for Updates…, Automatically, Quit) and system-menu items for 7.2/7.3/8.4 |
| 2.3 mode cards, double-click Play, arrows/space, accessory buttons, hints, Play title | done (card is custom-drawn, no UIA radio role: DEV) |
| 2.4 Graphics group, size list, presets, live preset indicator | done ("Full resolution (high DPI)" label) |
| 2.6 brightness presets and hint | done (via core) |
| 2.7 Server group summary | done |
| 2.8 Holomatch group, masters summary, Loopback Host Fix, cMod, HUD… | done |
| 2.9 Game Files group, patch warning, Play enablement | done (paths shown in full, no `~`: DEV) |
| 2.10 HUD sheet | done |
| 2.11/2.12 footer, Esc/Return, Ctrl+Q, Alt+F4 | done |
| 3 Play args, loopback wait (1.5 s), join address, co-op password in process | done |
| 3.9 Start Server, already-running alert, `--host-server` spawn | done |
| 4.2 chooser | done as a pre-dialog with in-place checkbox + "Choose Folder…" / "Choose GOG Installer…" (the spec's accepted equivalent) |
| 4.3/4.4 GOG unpack, copy, Add Game Files…, progress panel, alerts | done (alert texts from the core) |
| 5.x in-game File menu equivalents | not UI: core-a console commands |
| 6.1 Co-op sheet (hidden unless `lc_coop_ui_enabled()`), NAT, Check, Host, Connect | done (not verified end to end against a second PC) |
| 7.1 stvef:// registration, single-instance forwarding (WM_COPYDATA), pending link | done; link while the game (not launcher) runs starts a new launcher instead of the in-game dialogs: DEV |
| 7.2 About | done (system menu / gear menu) |
| 8 update check, badge, alerts, auto toggle | done (needs libcurl-4.dll next to the exe) |
| 9 test hooks | done: SNAPSHOT, APPEARANCE, MODE, PRESET, BRIGHTNESS, PLAY, AUTOPLAY, ADD, MASTERS, *_SNAPSHOT of masters/HUD/ABOUT/UPDATE_ALERT/server config/browser/co-op, BROWSE_CANCEL, SERVER_RUNNING, LINK, NODIALOG, CHOOSE, INPLACE, CONFIRM. Not done: LINK_HANDLER, PANEL_TIMEOUT, QUIT_* (in-game) |
| Per-Monitor-V2 DPI | done (main window rebuilds on WM_DPICHANGED; dialogs are sized at creation) |

## servers.md

| Item | Status |
|---|---|
| 4 Server Settings (4 pages, number fields, map cells with levelshots, drag/drop, Restore Defaults) | done; per-cell tooltips and the TICKRATE snapshot hook not done: DEV |
| 6 Master Servers (check, add/remove/move, drag live reorder, inline edit, restore) | done; per-row tooltips not done: DEV |
| 8 Browser (sources, sort, favourites, filters, who's playing, join, Expansion/password alerts, link row) | done; Map Info and 3D open efservers.com in the browser instead of a web view (as instructed); cell tooltips not done: DEV |
| 9 server window (state, info grid, players, Kick, Message, map/bot controls, colour console, command field, Play on This PC, router row, listing check) | done; window is fixed-size (not resizable): DEV; console text can't be selected (right-click Copy All): DEV |
| 10 Loopback Host Fix, public address | done (core) |
| Windows firewall rule | not done (installer's job) |
