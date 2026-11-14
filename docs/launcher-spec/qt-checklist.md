# Qt launcher (ef-launcher) checklist

Source: `EFAndroid-SP/app/jni/efcode/sys/qt/`. Spec: `launcher-data-coop.md` (LDC) and `servers.md` (SRV).
Legend: **done** = implemented as specified; **dev** = deviates (reason given); **n/a**.
Verified = rendered offscreen (Qt 6.4 dev container) and/or run; "untested" = code complete, not exercised.

## Main window (LDC §2)
| Item | State |
|---|---|
| Header, icon, three texts, 620 pt content, groups, footer, Return = Play, Esc = Quit | done (verified, light/dark) |
| Mode cards (select, double-click = Play, ←/→ wrap, Space, focus ring) | done |
| Co-op… accessory only when `lc_coop_ui_enabled()` | done |
| Graphics group: quality segments (Custom disabled), display, size list, high DPI, MSAA, texture, vsync, brightness + hint | done; label "Full resolution (high DPI)" as the spec allows |
| Server summary (Host mode), same height as Graphics | done |
| Holomatch group: name, masters summary + Servers…, Loopback Host Fix (stored at once), cMod checkbox + HUD… | done |
| Game Files group: all states, Show in File Manager, Add Game Files…, Choose…/Change…, Play enablement + tooltip | done |
| Update badge, Download…, tooltip | done |
| App menu (About, Check for Updates…, Automatically (checkable), Quit) | dev: a gear button in the header (no menu bar on the Deck) |
| HUD… sheet | done (verified) |
| About | done (QMessageBox, credits text reworded for Linux) (verified) |
| Window centered each show | done (first show) |
| Test background mode (`VOYAGERSP_TEST_BACKGROUND`) | dev: not implemented (no Dock/menu bar on Linux; offscreen platform covers tests) |

## Play, data, updates, links (LDC §3, §4, §7, §8)
| Item | State |
|---|---|
| Play: `lc_launch_build_argv` + `lc_exec` sibling `efsp`, `EF_FROM_LAUNCHER=1`, co-op password in `EF_COOP_PASSWORD` | done (verified with a stand-in efsp). `EF_LAUNCHER_DRYRUN=1` prints argv and exits (local test aid) |
| Start Server: running-servers alert (Show / Start Another / Cancel), spawn `--host-server` without `VOYAGERSP_TEST_*`, error alert | done; "Show Running Server" raises the window through a local socket |
| Chooser: folder or GOG installer, in-place checkbox + live status line, validation alerts, copy / unpack with progress + Cancel | dev: a small panel with "Choose Folder…" / "Choose GOG Installer…" (the spec allows two buttons) |
| Add Game Files… (files / folder / installer), nothing-to-add, disk space, progress | dev: same two-button pre-panel; texts come from the core |
| Check for Updates…, automatic check, throttling (core), badge | done (verified alert) |
| `stvef://` links: registration (`lc_link_register`, when `$APPIMAGE` and no handler), single-instance forwarding (QLocalServer), alerts | done; link row in the browser |
| `--choose-game-folder`, `--launcher`, `--host-server` | done |
| In-game File-menu equivalents | n/a (console commands, core-a) |

## Co-op sheet (LDC §6.1) — shown when `lc_coop_ui_enabled()` (EF_COOP builds: the release preset)
Layout, name, Host group (LAN list, password, background, router checkbox + status, Copy Invite), Join group (Check, result row, Munro, Character, Connect), Cancel: **done** (layout verified before 1.5.0; the Character row, pak compare and Join Anyway added in 1.5.0 are not yet compiled or run with Qt). Check compares pak0-pak3 with the host's like the Mac (`lc_coop_check_paks`): pak0-pak2 differing is a red "Your game files don’t match", only pak3 a yellow warning with **Join Anyway**. NAT / Check / Host / Connect logic: done, Check and NAT untested against a real host. **dev:** the router mapping is released when the launcher execs the game (the process is replaced), so a hosted mapping lives only until its lease ends.

## Server settings (SRV §4), masters (SRV §6), browser (SRV §8)
| Item | State |
|---|---|
| Server Settings: 4 pages, map list with thumbnails (JPEG via the engine's libjpeg, TGA decoder), search, add / remove / earlier / later, drag from the map list and reorder inside the rotation, number fields, modifier rules, hints, Restore Defaults, Done / Cancel | done (verified all pages) |
| Master Servers: rows with slot labels, inline edit on double-click, add / remove / move, drag to reorder, Check Now, restore buttons, live status | done (verified, real network). **dev:** drag reorder is Qt's InternalMove (moves on drop, not live while dragging) |
| Browser: source segments, spinner, status, filters, sortable table (star, lock, coloured names, cMod, ping colours), players panel, favourites, join (Expansion warning, password prompt), Connect address, link row | done (verified against live masters) |
| Map Info / 3D Map Preview | **dev:** open efservers.com in the browser (no embedded web view) |
| Snapshot hooks (config, masters, browser) | done |

## Server window (SRV §9), `ef-launcher --host-server`
Header + state, Server grid (address, Internet, Listed rows, map / game / players), controls (Next, Restart, Change Map menu, Add Bot, Remove Bots, Message…), players table + Kick (+ context menu), console with colours / auto-scroll / trimming, command field, Play on This Computer, Stop / Close, SIGTERM / close handling, running-servers registry, router mapping, listing check: **done** (verified with a stand-in efsp: start, ready, status, players, controls, stop). Router mapping and listing check are untested against a real router.

## Steam Deck / gamescope
Bigger fonts / controls (`DeckStyle`), scrolling content, maximized window, D-pad (arrow keys) focus navigation and A (Return) activation for buttons, combos, checkboxes, lists (`installDeckNavigation`), "Start the last mode automatically" (`autolaunch`, skipped with `--launcher` and when coming back from the game), first-run fullscreen, `in_joystick 1` and one-time `deck.cfg`: **done**; layout verified with `VOYAGERSP_TEST_DECK=1`; not run on a real Deck.

## Test hooks (LDC §9, SRV §14)
Implemented: SNAPSHOT, APPEARANCE, MODE, PRESET, BRIGHTNESS, PLAY, AUTOPLAY, ADD, MASTERS(+SNAPSHOT/ACTIONS), BROWSER_* (SNAPSHOT, TAB, SELECT, JOIN, EXPANSION, HOLD), BROWSE_CANCEL, SERVER_* (START, PORT, CONFIG_SNAPSHOT/TAB/TICKRATE, SNAPSHOT(_AFTER), STOP_AFTER, COMMAND, PLAY_AFTER, INTERNET, NOMASTER, ACTIONS, RUNNING), HUD, ABOUT, UPDATE / UPDATE_ALERT, LINK, LINK_HANDLER, COOP_*, NODIALOG, CHOOSE / INPLACE / CONFIRM. Not implemented: BACKGROUND, BROWSER_PREVIEW(_3D) (no web view), QUIT_* (in-game).
