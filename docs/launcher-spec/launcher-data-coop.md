# macOS launcher: functional specification (launcher, game data, GOG, co-op)

Scope: what the macOS launcher does, so that a Win32 launcher and a Qt 6 (Linux)
launcher can reach full feature parity on a shared portable C core. Written from
the source at commit `34193c9` (the `xplat-launcher` worktree). The server
browser, hosting, server Config… sheet, Servers… (master list) sheet and the
server status window (`sys_osx_server.m`) are specified separately; this
document only names their entry points.

Implementers should not need to read the Objective-C. Every user-visible string
is quoted exactly (curly quotes `’ “ ”`, the ellipsis `…`, `·` and `×` are the
real characters used). Line references use these abbreviations:

| Abbrev.   | File (under `EFAndroid-SP/app/jni/efcode/sys/`)          |
|-----------|----------------------------------------------------------|
| `L`       | `sys_osx_launcher.m` (2433 lines)                        |
| `LH`      | `sys_osx_launcher.h`                                     |
| `O`       | `sys_osx.m`                                              |
| `D`       | `sys_osx_data.m` (1764 lines)                            |
| `G`       | `sys_osx_gog.c` / `GH` `sys_osx_gog.h`                   |
| `C`       | `sys_osx_coop.m` (1461 lines) / `CH` `sys_osx_coop.h`    |
| `N`       | `sys_coop_net.c` (2177 lines) / `NH` `sys_coop_net.h`    |
| `M`       | `sys_main.c`                                             |
| `P`       | `sys_platform.h`, `PS` `sys_platform_stub.c`             |
| `SD`      | `sys_data.c` (Windows/Linux game-data lookup, 830 lines) |
| `S`       | `sys_osx_server.m` (other agent's scope; entry points only) |

---

## 0. Big picture

1. The app is one executable. `main()` (`M:862-1039`) runs, in order:
   `Sys_PlatformPreInit` → (headless? `Sys_PlatformResolveGameData(headless=1)`
   : `Sys_PlatformLauncher`, and if that returned false
   `Sys_PlatformResolveGameData(headless=0)`) → append `+set fs_basepath "<x>"` if
   a basepath came back → append argv → `Com_Init(commandLine)` →
   `Sys_PlatformPostInit` → main loop calling `Sys_PlatformFrame` every frame
   (`M:937-1036`).
2. The launcher is a **modal window shown in the game process before
   `Com_Init`**. Play does not spawn a process: the launcher appends `+set …`
   arguments to the engine's command-line buffer, hides itself, and the same
   process goes on to `Com_Init`. (Exception: Start Server spawns a second app
   instance, see §3.9.)
3. The launcher's arguments are appended **before** the user's own argv
   (`M:940-993`), so a `+set` on the real command line overrides the launcher's
   (later `+set` wins).
4. All graphics/name/cMod values the launcher passes are archived cvars, so the
   engine writes them back to the mode's config file (`efspconfig.cfg` /
   `hmconfig.cfg`). Next start the launcher reads them back from those files.
   **The cfg files are the source of truth for graphics; the launcher keeps no
   copy of them** (`L:28-33`). Changes made in the game's own Video menu thus
   show up in the launcher next time.
5. The game's own Quit (`quit` with no arguments) restarts the app at the
   launcher (relaunch), if the game was started from the launcher's Play
   (§5.1). Closing the window / Cmd+Q quit for good.
6. `commandLine` is `char[MAX_STRING_CHARS]` = **1024 bytes** (`M:866`,
   `q_shared.h:330`). Everything the launcher appends must fit; brightness is
   moved to a cfg file when it doesn't (§2.6).

### 0.1 Hooks (sys_platform.h)

`P:35-73` declares, for `EF_DESKTOP && !DEDICATED`:

| Hook | macOS implementation | Windows/Linux today (`PS`) |
|---|---|---|
| `void Sys_PlatformPreInit(argc, argv, commandLine, size)` | `O:559-567`: disable press-and-hold, bundled Vulkan env, homepath migration, appends `+set com_homepath EliteForce-MacOS ` unless argv has `fs_homepath`/`com_homepath` | `PS:22-31`: appends `+set com_homepath "EliteForce-Marino" ` unless argv names `fs_homepath`/`com_homepath` |
| `qboolean Sys_PlatformLauncher(argc, argv, commandLine, size)` | `Sys_MacLauncher` (`L:2234-2433`) | returns `qfalse` ("no launcher yet") |
| `const char *Sys_PlatformResolveGameData(argc, argv, headless)` | headless: `Sys_MacHeadlessGameData` (`O:295-320`); else `Sys_MacResolveGameData` (`D:1143-1183`, may show the chooser) | `Sys_DataResolve` (`SD:789-828`, never UI except an error box) |
| `void Sys_PlatformPostInit(void)` | `Sys_MacPrintStartupInfo` + `Sys_MacDataPostInit` (replay early log, register `macos_*` commands, menu-tracking sound pause) | `Sys_DataPrintEarlyLog` |
| `void Sys_PlatformQuitSignals(void)` | dispatch sources for SIGTERM/SIGINT (`O:63-83`) | nothing |
| `void Sys_PlatformFrame(void)` | `Sys_MacFrame` (`D:1566-1615`): adds the File menu, test hooks, `macos_debugpointer` | nothing |
| `void Sys_PlatformQuitToLauncher(void)` | `Sys_MacQuitToLauncher` (`D:1231-1239`) | nothing |

`Sys_PlatformQuitToLauncher` is called from `Com_Quit_f` (`qcommon/common.c:412-422`)
only when the command is exactly `quit` with no arguments (`Cmd_Argc()==1`).
The window-close confirmation and Cmd+Q send `quit Closed window`, which does
**not** return to the launcher.

Co-op engine hooks (EF_COOP builds only): `Sys_CoopFrame()` is called once per
`Com_Frame` (`common.c:3555-3561`); `Sys_CoopBackgroundPlay()` disables the
unfocused/minimized fps throttle (`common.c:3426-3434`). Both live in
`sys_osx_coop.m` today (§6.3) and must move to portable C.

---

## 1. Persistent settings inventory

### 1.1 NSUserDefaults keys (app's bundle-id domain)

A portable launcher needs an equivalent store. Suggested: a `launcher.cfg`
key/value file in the homepath (sys_data.c already reads `gamefolder` from it,
§4.6), or the registry / QSettings. Keys:

| Key | Type / default | Meaning | Read | Written | Ref |
|---|---|---|---|---|---|
| `LauncherMode` | int 0/1/2, default 0 (invalid → 0) | last mode: 0 Single Player, 1 Holomatch, 2 Host a Server | launcher init | on Play (OK) after the modal ends; on Start Server (sets 2) | `L:101,685-687,1385,2426` |
| `CheckForUpdates` | bool, absent = YES | automatic update check | init, menu validation | "Check for Updates Automatically" toggle | `L:102,1766-1770,1872-1882` |
| `UpdateLastCheck` | date | time of the last successful check | init | after a successful fetch (not with the test hook) | `L:103,1813` |
| `UpdateLatest` | dict `{version, url}` | latest release found at last check | init (badge shown at once) | after a successful fetch; removed when none found | `L:104,1814-1817` |
| `HudWidescreen` | bool, absent = widescreen (1) | HUD… sheet's `cg_aspectCorrect` choice | init | HUD… Done | `L:105,706-707,1539` |
| `GameFolder` | string path (folder that **holds** BaseEF) | "use in place" game folder | `VSP_FindGameData`, Add Game Files | chooser (in place: set; copy or GOG: removed); Delete Saves & Settings (removed) | `D:97,759,791,802,868,964,1437` |
| `ChooserPanelSizeReset` | bool | one-time reset of the open panel's remembered size (also removes Apple's `NSNavPanelExpandedSizeForOpenMode`) | chooser | chooser, once | `D:98,713-718` (macOS-only quirk; not needed elsewhere) |
| `ApplePressAndHoldEnabled` | registered default NO | disables the accent picker on key hold | – | registerDefaults at start | `O:94-99` (macOS-only) |
| `CoopKeepPlayingInBackground` | bool, absent = YES | co-op Host "Keep playing in the background" | sheet build | Host pressed | `C:801-802,959-960,1262` |
| `CoopPlayerName` | string | co-op "Your name" (stored after cleaning; may be empty) | sheet build | Host or Connect | `C:804,898-901,916` |
| `CoopGuestMunro` | `"male"`/`"female"` | co-op Join "Munro" choice | sheet build | Connect | `C:805,1033-1034,1301` |
| `CoopGuestCharacter` | string: a `coop_characters.h` id, `""` = Munro | co-op Join "Character" choice | sheet build | Connect | `lookPopup`, `connect:` |
| `ServerSettings` | dict | Host a Server settings (`VSPServerSettings`) | init | Config… Done / Start Server | `S:113` (other spec) |
| `MasterServers` | array of strings; absent = defaults | Servers… list | init, Play | Servers… sheet | `S:3845-3911` (other spec) |
| `LoopbackHostFix` | bool; absent = YES (only `NO` is stored) | Loopback Host Fix checkbox | init, Play | checkbox toggle | `S:4471-4491` |
| `BrowserFavorites`, `BrowserOnlyFavorites`, `BrowserOnlyPeople` | | Browse… sheet | | | `S:4472-4474` (other spec) |

### 1.2 Files the launcher reads or writes

Homepath on macOS: `$HOME/Library/Application Support/EliteForce-MacOS`
(`HOME` env first, else `NSHomeDirectory()`; `D:143-149`). "HomeBase" =
`<homepath>/baseEF` (`BASEGAME` is `"baseEF"`). Windows: `%APPDATA%\EliteForce-Marino`;
Linux: `$XDG_DATA_HOME/eliteforce-marino` or `~/.local/share/eliteforce-marino`
(lower case, spaces → hyphens; `SD:351-382`).

| File | R/W | When | Content |
|---|---|---|---|
| `<HomeBase>/efspconfig.cfg` | read | launcher init (SP mode, or fallback) | engine's SP config; graphics read from it (§2.5) |
| `<HomeBase>/hmconfig.cfg` | read | launcher init (HM/Host mode, or fallback); always for name and cMod values | Holomatch config |
| `<HomeBase>/vsp_masters.cfg` | write | every Holomatch Play / every server client-args build | `set sv_master1 "…"` … `set sv_master16 "…"` (other spec, `S:3913-3925`) |
| `<HomeBase>/vsp_brightness.cfg` | write | Play, only when brightness `+set`s don't fit on the command line | see §2.6 |
| `<HomeBase>/*.pk3`, `<HomeBase>/saves/**` | write | chooser copy, GOG unpack, Add Game Files | §4 |
| `<homepath>` (whole folder) | moved to Trash | File > Delete Saves & Settings… | §5.4 |

Config file names are per **mode**: Single Player uses `efspconfig.cfg`;
Holomatch **and** Host a Server use `hmconfig.cfg` (a server's "Play on This
Mac" is a Holomatch client) (`L:193-196,691-692`).

---

## 2. Main launcher window

### 2.1 Window

`L:755-1044`.

- Size: content 620 pt wide (fixed by constraint `L:1041`), height from layout
  (initial rect 620×600). Not resizable, not minimizable.
- Style: titled + closable + full-size content view; transparent title bar,
  title hidden (title string "Elite Force"); movable by dragging the background.
- Close button = Quit (`windowShouldClose` → `quit:`, returns NO; `L:1360-1364`).
- Centered every time it is shown (`L:2406`). Shown key + front (except in the
  background test mode).
- Initial first responder: the selected mode card (`L:1043`).
- Root layout: vertical stack, edge insets top 30, left 28, bottom 20, right 28,
  spacing 16, with 20 after the header and 20 after the "Game Files" group
  (`L:1020-1027`). Every child is root width − 56.

Order of sections, top to bottom:

1. Header
2. Mode cards row
3. "GRAPHICS" group **or** "SERVER" group (they swap; the Server group's box is
   constrained to the Graphics group's box height so the window doesn't change
   size; `L:1028-1031`)
4. "HOLOMATCH" group
5. "GAME FILES" group
6. Footer

**Group style** (`VSP_Group`, `L:416-463`): caption above a rounded box. Caption
is the given text upper-cased, 11 pt semibold, secondary label color. Box:
corner radius 10, fill = white 5 % alpha in dark mode / black 3.5 % alpha in
light mode, 1 px stroke in the system separator color; content inset 14
left/right, 12 top/bottom; caption-to-box spacing 6.

**Dark mode**: everything uses dynamic system colors (label, secondary,
tertiary, separator, accent, system orange/green/red). Light/dark follows the
OS. Test hook `VOYAGERSP_TEST_LAUNCHER_APPEARANCE=dark|light` forces one
(`L:2183-2193`).

### 2.2 Header

`L:777-792`, `L:1662-1701`. Horizontal row, spacing 14, centered vertically:

1. App icon, 56×56.
2. Text column (spacing 2):
   - "Star Trek: Voyager" — 12 pt semibold, secondary color
   - "Elite Force" — 24 pt bold
   - "Choose how you want to play." — 13 pt, secondary color
3. Flexible spacer.
4. **Update badge** (hidden unless a newer release is known, §8):
   rounded box (radius 9, no border, fill = accent color at 13 % alpha),
   padding 7 top/bottom, 10 left/right, row spacing 8:
   - icon `arrow.down.circle.fill`, 20 pt, accent tint
   - text column (spacing 1): "Update available" (12 pt semibold) over
     `"Version <new> · you have <mine>"` (11 pt, secondary)
   - button "Download…" (small) — tooltip "Open the release page on GitHub";
     opens the stored release URL in the browser.
   - Badge tooltip: `"Elite Force for Mac <new> is on GitHub"`.

### 2.3 Mode cards

`L:467-605` (card), `L:794-818` (row). Three equal-width cards in a row,
spacing 10, top-aligned, equal height (bottom-pinned to the row).

Each card: rounded rect radius 12. Unselected: group fill + 1 px separator
stroke; icon tinted secondary. Selected: fill = accent at 14 % alpha, 2 px
accent stroke; icon tinted accent. Layout: icon (SF Symbol 19 pt, width 30) +
title (15 pt semibold) on one line, inset 14 left / 13 top; detail text
(12 pt, secondary, wraps at 150) 6 below; optional accessory button 14 left,
11 from the bottom, ≥ 8 below the detail.

| Card | Symbol | Title | Detail | Accessory |
|---|---|---|---|---|
| Single Player (mode 0) | `person.fill` | "Single Player" | "The Voyager campaign, your saves and the Virtual Voyager." | "Co-op…" button — **EF_COOP builds only** (otherwise no accessory) (§6) |
| Holomatch (mode 1) | `person.3.fill` | "Holomatch" | "Join games on your network or the internet, or play with bots." | "Browse…" |
| Host a Server (mode 2) | `server.rack` | "Host a Server" | "Run a Holomatch server on this Mac for friends." | "Config…" |

Accessory buttons are small control size, small system font:

- **Co-op…** tooltip "Play the campaign with a friend: host a co-op game, or check for one to join (experimental)" (`C:1427-1430`); opens the Co-op sheet (§6.1).
- **Browse…** tooltip "See the games on your network and the internet, and join one". Action (`L:1225-1234`): select Holomatch mode, then open the server browser sheet `VSP_ShowServerBrowser(window, dataFolder, done)` (S). If the user picks a server, `done(address, password)`: the launcher remembers `joinAddress`/`joinPassword`, selects Holomatch and presses Play immediately (§3).
- **Config…** tooltip "Maps, game type, bots and the server's name, password and network". Action (`L:1202-1208`): select Host mode, open `VSP_ShowServerConfig(window, server, dataFolder, done)` (S); on any close, refresh the Server group summary.

Card interaction (`L:569-603`):

- Mouse down: select that mode. Double-click: select **and Play**.
- Focusable (accepts first responder, focus ring = rounded rect). Left arrow /
  Right arrow: select previous / next mode, wrapping (0↔2). Space: select.
- Accepts first mouse (a click on the inactive window acts at once).
- Accessibility: role radio button, label = title, help = detail, value =
  selected.

**selectMode(m)** (`L:1127-1149`) updates:

- card selection;
- player name field enabled iff mode ∈ {Holomatch, Host}; its label color
  label vs. tertiary;
- cMod checkbox enabled iff mode ∈ {HM, Host} **and** cMod modules are bundled
  (`VSP_CmodAvailable`, S:124-132: any `*.pk3` in `Contents/Resources/cmod`);
  its "Game modules:" label color likewise; then HUD… enablement (§2.8);
- name hint text: HM "Shown to other players in Holomatch."; Host "Your name
  when you use Play on This Mac."; SP "Used when you play Holomatch.";
- Play button title: SP "Play", HM "Play Holomatch", Host "Start Server";
- Graphics group hidden iff Host; Server group shown iff Host (and refreshed);
- keyboard focus follows to the new card if a card had focus.

Mode is **not** saved on selection, only on Play / Start Server.

### 2.4 Graphics group (modes SP and Holomatch)

`L:820-863`. A two-column grid: right-aligned labels (13 pt, column width 112),
column spacing 10, row spacing 7, first-baseline aligned, 4 extra below row 0.

| Row | Label | Control | Values (title → value) |
|---|---|---|---|
| 0 | "Quality:" | segmented control | "Performance", "Balanced", "Best", "Custom" (Custom segment is disabled: never user-selectable, only shown) |
| 1 | "Display:" | popup | "Window" → 0, "Full Screen" → 1 (`r_fullscreen`) |
| 2 | "Window size:" | popup | list of sizes (below); **disabled when Full Screen** |
| 3 | – | checkbox | "Full Retina resolution" (`r_highdpi`) |
| 4 | "Anti-aliasing:" | popup | "Off"→0, "2×"→2, "4×"→4, "8×"→8 (`r_ext_multisample`) |
| 5 | "Texture detail:" | popup | "High"→0, "Medium"→1, "Low"→2 (`r_picmip`) |
| 6 | – | checkbox | "Sync to the display (V-Sync)" (`r_swapInterval`) |
| 7 | "Brightness:" | popup | "Standard", "Plain", "Full Contrast", "Extra Bright", "Full Bright", "Custom" (Custom item disabled) |
| 8 | – | hint label | 11 pt secondary, wraps at 400: the selected preset's help text (table §2.6) or for Custom "Set in the game (its Brightness slider, say); Play leaves it as it is." |

Port note: "Full Retina resolution" = high-DPI rendering at native pixel
density (`r_highdpi 1`); Windows/Linux can label it e.g. "Full resolution
(high DPI)" but keep the same cvar.

**Window size list** (`L:178-181`, `L:1047-1070`): candidates
1024×768, 1280×720, 1280×800, 1440×900, 1600×900, 1680×1050, 1920×1080,
1920×1200, 2560×1440, 2560×1600 (points / logical pixels). A size is listed only
if `w ≤ visibleWidth` and `h ≤ visibleHeight − 28` of the main screen's usable
area (menu bar and Dock excluded; 28 = title bar), **except** the currently
configured size, which is always listed; if it isn't one of the candidates it is
appended at the end. Title format `"%d × %d"` (U+00D7). Item tag
`w*10000+h`.

**Quality presets** (`L:122,172-176`, `L:1072-1080`, `L:1114-1125`). A preset
sets four values; it does not touch display, window size or brightness:

| Preset | `r_highdpi` | `r_ext_multisample` (MSAA) | `r_picmip` | `r_swapInterval` |
|---|---|---|---|---|
| Performance | 0 | 0 | 1 (Medium) | 0 |
| Balanced | 0 | 2 | 0 (High) | 1 |
| Best | 1 | 4 | 0 (High) | 1 |

The segmented control always shows the preset whose four values all match the
current ones, else "Custom". Choosing Performance/Balanced/Best applies its
values and refreshes all controls. Changing any graphics control re-derives the
whole `gfx` state from the controls (`graphicsChanged`, `L:1099-1112`) and
re-shows it (so the preset indicator updates live). Graphics changes are not
saved anywhere by the launcher; they reach the cfg files via Play.

### 2.5 Graphics state: loading (first run, config parsing)

**Config parsing** (`ReadConfig`, `L:198-222`): read the file as UTF-8, else
Latin-1. For each line: first whitespace-delimited token must be `seta` or
`set`; second token is the name (stored lower-cased); value is either the text
between the next `"` and the following `"`, or the next whitespace-delimited
token; missing → `""`. Later lines win. `CfgInt(name, def)` = integer value
(leading-number parse) when the key exists, else def; `CfgFloat(name, def)` =
float when the value is non-empty, else def.

**LoadGraphics(holomatch)** (`L:333-369`), done **once at launcher start** with
the mode remembered at that time (HM and Host read `hmconfig.cfg`; SP reads
`efspconfig.cfg`). Switching mode in the window does **not** reload graphics
from the other file (a simplification; parity means keeping it).

1. `cfg = ReadConfig(modeConfig)`; if it has no entries, `cfg =
   ReadConfig(otherModeConfig)`. `fromConfig = cfg non-empty`.
2. `brightness = MatchBrightness(cfg)` (§2.6). First run → Standard.
3. `fullscreen = r_fullscreen != 0` (default 0).
4. Size: default 1920×1080. If `r_mode == -1` (absent counts as −1) and
   `r_customwidth ≥ 640` and `r_customheight ≥ 480`, use those.
5. If `fromConfig`: `highdpi = r_highdpi != 0`; `msaa = r_fbo != 0 ?
   r_ext_multisample : 0`; `picmip = clamp(r_picmip, 0, 2)`;
   `vsync = r_swapinterval != 0`. Else (**first run**): the **Best** preset
   (highdpi 1, msaa 4, picmip 0, vsync 1) — "Apple Silicon has power to spare".
   Port note: a Windows/Linux first-run default of Best is the parity choice;
   whether to lower it on weak GPUs is a product decision.
6. MSAA normalisation: if not 0/2/4/8 → `>4 → 8`, `>2 → 4`, else `2`.

### 2.6 Brightness presets (from cMod by Noah Metzger)

`L:135-170` table, `L:249-278` match, `L:280-330` args.

| # | Title | Help text (hint row) | test id | `r_gamma` | `r_overBrightFactor` | `r_mapLightingFactor` | `r_mapLightingGamma` | `r_mapLightingClampMin` | `r_intensity` |
|---|---|---|---|---|---|---|---|---|---|
| 0 | Standard | "This Mac version's own look: the original lighting, without overbright." | `standard` | 1 | 0 | 0 | 1 | 0 | `1` |
| 1 | Plain | "From cMod: safe values that work well with almost all maps." | `plain` | 1.4 | 1.5 | 2 | 1 | 0 | `1` |
| 2 | Full Contrast | "From cMod: the original Elite Force look. Good, but parts of some maps can be too bright." | `contrast` | 1.3 | 2 | 2 | 1 | 0 | `1` |
| 3 | Extra Bright | "From cMod: for better visibility in dark places." | `bright` | 1.4 | 1.5 | 2 | 1.75 | 0 | `1.1:0.5` |
| 4 | Full Bright | "From cMod: no shadows or lighting effects. Results vary between maps." | `fullbright` | 1.1 | 1 | 2 | 10 | 1 | `1.4:0.5` |
| – | Custom | "Set in the game (its Brightness slider, say); Play leaves it as it is." | – | (no `+set`s at all) | | | | | |

(Port note: the Standard help text says "This Mac version's"; Windows/Linux
should say e.g. "This version's own look: …". A factor of 0 means "follow
`r_overBrightBits` / `r_mapOverBrightBits`", which Standard forces to 0.)

**Args for preset b** (`BrightnessSets`, `L:283-303`), in this order, numbers
formatted with C `%g` (1.0 → `1`, 1.75 → `1.75`, 10 → `10`):

```
+set r_gamma <g> +set r_overBrightFactor <obf> +set r_mapLightingFactor <mlf>
+set r_mapLightingGamma <mg> +set r_mapLightingClampMin <cmin>
+set r_mapLightingClampMax 1 +set r_intensity <intensity> +set r_textureGamma 1
[+set r_overBrightBits 0]       (only when obf == 0, i.e. Standard)
[+set r_mapOverBrightBits 0]    (only when mlf == 0, i.e. Standard)
```

**Match** (`MatchBrightness`, `L:250-278`); all comparisons with tolerance
`|a−b| < 0.005`; absent cvars take the defaults shown:

1. If `r_maplightingclampmax` (default 1) ≠ 1 or `r_texturegamma` (default 1) ≠ 1 → Custom.
2. `r_intensity` (default `"1"`) is split at `:` into value (empty → 1) and
   fraction (absent → 1).
3. For each preset i in order: match if `r_gamma`(1) ≈ g,
   `max(r_overbrightfactor(0),0)` ≈ obf, `max(r_maplightingfactor(0),0)` ≈ mlf,
   `r_maplightinggamma`(1) ≈ mg, `r_maplightingclampmin`(0) ≈ cmin, intensity
   value ≈ preset's, and (if the preset's intensity value ≠ 1) fraction ≈ the
   preset's fraction; **and** if the preset's obf is 0 then
   `r_overbrightbits`(0) must be 0, if its mlf is 0 then `r_mapoverbrightbits`(0)
   must be 0. First match wins.
4. None → Custom (−1).

**Command-line room** (`BrightnessArgs`, `L:309-330`). Given `room` bytes left:
if the args are empty (Custom) → nothing. If `len(args joined with " ") + 2 <
room` → pass them inline. Else write `<HomeBase>/vsp_brightness.cfg`:

```
// Written by the Elite Force launcher: its Brightness preset. Edits here are overwritten.
set r_gamma "1.4"
set r_overBrightFactor "1.5"
… (one `set <name> "<value>"` line per +set triple)
```

(creating HomeBase if needed) and pass `+exec vsp_brightness.cfg` instead; log
"Launcher: brightness in vsp_brightness.cfg (the command line is full)". On a
write failure: log "Couldn't write <path>: brightness left as it is" and pass
nothing. (Via `+exec` the latched renderer cvars apply from the first map on
rather than in the menus.)

### 2.7 Server group (mode Host a Server only)

Summary of the Config… settings (`L:865-893`, `L:1151-1200`). Grid identical in
style to Graphics (label column 112, 13 pt). Values are single-line labels,
truncating at the tail.

| Label | Value |
|---|---|
| "Server name:" | `server.hostname` |
| "Game:" | game type name (`VSPServerSettings gametypeName:`) and, if any modifiers are on, `" · "` + their names joined by `", "` |
| "Maps:" | 0 maps: "None chosen yet" (orange). 1 map: its display name, or "1 missing map" (orange). N: `"N in rotation: A, B, C"` (names of the maps found), + `" (K missing)"` if K>0 (orange). Tooltip: map names joined by `" → "` |
| "Players:" | `"Up to <maxPlayers>"` + (bots ? `" · bots fill to <bots>[ per team] (<skill name>)"` where " per team" iff gametype ≥ Team (3) : `" · no bots"`) |
| "Network:" | `"<Internet, port opened on the router \| Internet \| Local network only> · port <port>"` + `" (not listed in the Join menu)"` if port ∉ 27960…27963 (then orange) + `" · password"` if a password is set |
| "Address:" | `"<LAN IPv4 or 127.0.0.1>:<port>"`, 13 pt monospaced-digit medium |
| – | tip (11 pt secondary, wrap 400): "Friends connect to this address, or find the server in Holomatch's Join menu. It runs in its own window, where Play on This Mac joins it with your Holomatch graphics settings." |

Data comes from S: `VSP_MapCatalog(dataFolder)`, `VSP_FindMap`, `VSPMapInfo.displayName`,
`VSPServerSettings` (`modifierNames`, `gametypeName:`, `skillName:`),
`VSP_LANAddress()`.

### 2.8 Holomatch group

`L:895-942`. Grid, label column 112 right-aligned, row spacing 4; extra top
padding 8 before row 2 and row 5, 4 before row 3.

| Row | Label | Control |
|---|---|---|
| 0 | "Player name:" (13 pt; tertiary color in SP mode) | text field, width 220, placeholder "UnnamedPlayer"; disabled in SP mode |
| 1 | – | hint (11 pt secondary), per mode (see selectMode) |
| 2 | "Internet games:" | master summary label (13 pt, tail-truncating) + small button "Servers…" |
| 3 | – | checkbox "Loopback Host Fix" |
| 4 | – | 11 pt secondary: "Shows a server you host on this Mac in your own internet lists." |
| 5 | "Game modules:" (tertiary when disabled) | checkbox "Use cMod's game modules, by Noah Metzger" + small button "HUD…" |

- **Player name** initial value (`L:711`): `hmconfig.cfg`'s `name` unless absent
  or `"UnnamedPlayer"`, else **DefaultPlayerName** = first space-separated word
  of the user's full name (`NSFullUserName`), or `"Crewman"` if empty
  (`L:371-376`). Port: Windows `GetUserNameExW(NameDisplay)` → first word, else
  `%USERNAME%`; Linux GECOS field of `getpwuid` → first word, else the login.
- **Name cleaning** `VSP_CleanName` (`L:379-392`): keep characters with code
  32…126 except `"` `+` `;` `\` `%`, at most 31 kept characters; if the result
  is blank/whitespace-only → `"UnnamedPlayer"`. (Non-ASCII is dropped, not
  transliterated.) The name is not saved by the launcher; it reaches
  hmconfig.cfg via `+set name` (Holomatch Play only).
- **Master summary** (`L:1210-1218`): 0 → "No master servers" (orange);
  1 → `"1 master server · <m1>"`; N → `"N master servers · Internet 1: <m1>"`;
  tooltip = all masters, one per line. Data: `VSP_MasterServers()` (S).
  **Servers…** (small) tooltip "The master servers that list internet games, and
  that a server you host announces itself to" → `VSP_ShowMasterServers(window,
  done)` (S); on close refresh the summary (`L:1244-1249`).
- **Loopback Host Fix** tooltip "Most routers don't route this Mac back to its own public address, so a server hosted here wouldn't answer in your own internet lists. This lists it as this Mac (127.0.0.1) instead." Initial state `VSP_LoopbackFix()`; toggling calls `VSP_SetLoopbackFix(on)` (stored at once; `L:1220-1223`). At init the launcher starts `VSP_StartPublicAddressLookup()` (NAT-PMP address-only request, S:4501-4516) so the public IP is ready by Play. Enabled in all modes. Engine side: `cl_loopbackHostIP` makes `client/cl_main.c:2775-2797` list servers whose address equals it as 127.0.0.1.
- **cMod checkbox** tooltip "cMod's cgame and ui in place of the original ones (not on pure servers, and never in place of a mod's own): a widescreen HUD and menus, weapon fire prediction and many fixes." Initial state: `hmconfig.cfg` `cl_cmodmodules` (default 1 → on) (`L:702`). Enabled per selectMode. Toggling only updates HUD… enablement (value used at Play). Engine: `cl_cmodModules` (`qcommon/files.c:4017`, CVAR_ARCHIVE, default 1).
- **HUD…** (small) tooltip "cMod's widescreen HUD and weapon fire prediction". Enabled iff cMod checkbox enabled **and** on (`L:1471-1474`). Opens the HUD sheet (§2.10).

cMod values held by the launcher (`L:700-710`):

| Field | Source at init | Default |
|---|---|---|
| `hudAspect` (`cg_aspectCorrect`) | NSUserDefaults `HudWidescreen` — **not** hmconfig (cMod's modules register it with 0, so a stale 0 may sit in hmconfig) | 1 |
| `hudCenter` (`cg_aspectCorrectCenterHud`, a **string**: 0…1, or cMod's `"16:9*"`-style form) | hmconfig `cg_aspectcorrectcenterhud` if non-empty | `"0"` |
| `predictWeapons` (`cg_predictWeapons`) | hmconfig `cg_predictweapons` | 1 |

### 2.9 Game Files group

`L:944-988`, `L:1251-1353`. Vertical: an info row (36×36 folder icon + text
column, spacing 10) over a right-aligned button row (spacing 8).

Text column (spacing 3):

1. Folder path: 12 pt medium, middle-truncating, tooltip = full BaseEF path.
2. Status row: SF symbol icon + 11 pt secondary text, tail-truncating.
3. Expansion row: icon + 11 pt secondary text.

Buttons: "Show in Finder", "Add Game Files…" (tooltip "Copy more .pk3 files
into the game, such as the Expansion Pack's or extra maps"), "Change…" /
"Choose…". (Port: "Show in Explorer" / "Show in File Manager" / "Open Folder".)

**refreshData** (`L:1251-1329`) — called at init and after Add/Change:

Case A — the command line overrides the data paths (`VSP_DataOverridden`: any
argv element equal (case-insensitive) to `fs_basepath`, `fs_homepath`,
`com_homepath`, `fs_cdpath`, `fs_steampath`, `fs_gogpath`, `com_basegame`;
`D:837-848`): haveData = yes; basepath = dataFolder = nil; path "Set on the
command line"; status "fs_basepath / fs_homepath given on the command line",
icon `terminal`, secondary color.

Case B — otherwise `VSP_FindGameData(&basepath, &dataFolder)` (§4.1):

| State | Path text | Status text | Icon / color |
|---|---|---|---|
| found, but `pak1.pk3` and/or `pak2.pk3` missing in both HomeBase and dataFolder | dataFolder with `~` | `"Patch 1.2 missing · add <pak1.pk3 and pak2.pk3> with Add Game Files…"` (missing names joined by `" and "`); tooltip: "These game files are from the original CD (version 1.0). The game also needs <names> from the 1.1 and 1.2 patches: copy them from a patched installation (GOG, Steam, or a PC with patch 1.2) with Add Game Files…." | `exclamationmark.triangle.fill`, orange. Logged `Launcher: patch files missing (…) in "…"` |
| found, basepath non-nil (in place, or next to the app) | dataFolder with `~` | "Ready · using the files in this folder" | `checkmark.circle.fill`, green |
| found, basepath nil (homepath copy) | dataFolder with `~` | "Ready · copied into the app's own folder" | same |
| not found | "No game files yet" | "Choose the Elite Force folder (the one with BaseEF), or the GOG setup .exe." | `exclamationmark.triangle.fill`, orange |

Then:

- Folder icon: the OS icon of dataFolder, or a generic folder icon.
- "Show in Finder" hidden iff dataFolder is nil. Action: open dataFolder in the
  file manager (`VSP_RevealFolder` opens the folder; `D:915-919`).
- "Add Game Files…" hidden iff !haveData or overridden. Action §4.4, then
  refreshData and re-front the launcher.
- Expansion row hidden iff !haveData or overridden; else
  `VSP_HasExpansion(dataFolder)` (§4.5): yes → "Expansion Pack found · Virtual
  Voyager and its Holomatch maps", icon `puzzlepiece.extension.fill` green; no →
  "No Expansion Pack · add its files to play Virtual Voyager and its maps", icon
  `puzzlepiece.extension` tertiary.
- Change button: disabled iff overridden; title "Change…" if haveData else
  "Choose…". Action: the chooser (§4.2) (log "Launcher: game folder changed" on
  success), then refreshData and re-front.
- **Play enabled iff haveData && !missingPatch**; Play tooltip "Add the 1.2
  patch files first (see Game Files)" when patch missing, else none. (The engine
  would otherwise stop at once in `FS_CheckPak0`.)

### 2.10 HUD… sheet (cMod's HUD settings)

`L:1476-1574`. Sheet on the launcher window, content width 480, title "HUD".
Grid (labels 13 pt right-aligned, row spacing 10), insets 20/20/16/20, then a
button row (spacing 18 above).

| Row | Label | Control |
|---|---|---|
| 0 | "HUD:" | popup: "Widescreen: sharp, in proportion" (→1), "Stretched to the screen (original look)" (→0); initial = hudAspect |
| 1 | "Position:" | "Edges" (11 pt secondary) – slider 0…1, 5 tick marks, width 200 – "Middle"; initial = hudCenter if it parses as a float in [0,1] else 0; **enabled only while the popup is Widescreen** |
| 2 | "Weapons:" | checkbox "Show my shots at once on servers that offer it" (predictWeapons) |
| 3 | – | hint 11 pt secondary, wrap 300: "From cMod's game modules by Noah Metzger. Weapon fire prediction needs a server running cMod's game code with it on (cMod servers, and Host a Server's default)." |

Buttons (right-aligned): "Cancel" (Esc) and "Done" (Return, default).

Done: `hudAspect = popup value`, saved to `HudWidescreen` (bool). **Only if the
slider was moved** (any slider action event): `hudCenter =
"%g" of round(value*100)/100` (else the old string, e.g. `"16:9*"`, is kept).
`predictWeapons = checkbox`. These are not saved anywhere else; they reach
hmconfig.cfg through the command line at Play. Cancel: no change.

(With the original, non-cMod modules the engine's own widescreen layer
`cl_flexibleDisplay` keeps the 4:3 HUD centered; `L:1476-1478`.)

### 2.11 Footer

`L:990-1004`. Left: `"Version <CFBundleShortVersionString> · Made by Marino
Hardin"` (or "Made by Marino Hardin" without a version), 11 pt tertiary. Spacer.
"Quit" (large, key equivalent **Esc**). "Play" (large, min width 110, key
equivalent **Return** → default button; title per mode).

- Quit: ends the launcher with Cancel → log "Launcher: quit", `SDL_Quit()`,
  `exit(0)` (`L:1355-1358`, `L:2420-2425`).
- Play (`L:1366-1374`): ignored if !haveData or missingPatch. Host mode →
  Start Server (§3.9; the launcher stays up). Else end the modal with OK (§3).

### 2.12 Keyboard summary (main window)

| Key | Effect |
|---|---|
| Return | Play / Play Holomatch / Start Server |
| Esc | Quit |
| Cmd+Q | Quit (a local key monitor, because SDL's Quit item only posts an SDL event; `L:2341-2350`) — port: Alt+F4 / Ctrl+Q |
| ← / → on a focused mode card | previous / next mode (wraps) |
| Space on a card | select it |
| Double-click a card | select and Play |
| Tab | standard focus loop |

---

## 3. Play: mode, command line, process flow

### 3.1 When the launcher runs at all (`Sys_MacLauncher`, `L:2234-2433`)

1. If any argv element equals (case-insensitive) `com_holomatch` → return
   false (no launcher; the mode is on the command line — this is how relaunches
   from the File menu and tests skip it). `main` then resolves game data itself.
2. `hostOnly` = argv contains exactly `--host-server` (`VSP_HOST_SERVER_ARG`), or
   env `VOYAGERSP_TEST_SERVER_START` is set.
3. Not running from an `.app` bundle → return false. (Port: Windows/Linux
   always have a launcher unless the command line names the mode.)
4. Init SDL video (on macOS this creates NSApp; needed for any UI).
5. Build the launcher (reads configs/defaults, starts the update check, the NAT
   public-address lookup, etc.).
6. Test hooks (§9).
7. **hostOnly**: show only the server window
   `VSP_RunServerWindow(server, basepath, dataFolder, clientArgs)` (S), then
   `SDL_Quit(); exit(0)` — never the launcher, never the game. Logs "Server: no
   game files found" when there is no data.
8. Install the `stvef://` link handler (§7.1).
9. Install the Cmd+Q monitor; retarget the app menu for the modal session
   (§7.3); insert the update menu items (§8.4); center, activate, show; run the
   window modally.
10. After the modal ends: remove the update items, restore the app menu, hide
    the window. Cancel → `exit(0)`. OK → save `LauncherMode`, `appendTo`
    (§3.2), `Sys_MacSetFromLauncher()` (so the game's Quit returns here, §5.1),
    shut SDL video down, return true.

### 3.2 Arguments appended on Play (`appendTo`, `L:1603-1646`)

`holomatch = (mode != Single Player)`. Appended in this exact order, each item
followed by a space (values in quotes exactly as shown):

```
+set com_holomatch <0|1>
+set r_fullscreen <0|1> +set r_mode -1 +set r_customwidth <w> +set r_customheight <h>
+set r_highdpi <0|1> +set r_picmip <0..2> +set r_swapInterval <0|1>
+set r_fbo 1 +set r_ext_multisample <0|2|4|8>
[HM]  +set name "<VSP_CleanName(name field)>"
[HM]  <cmodArgs, each token followed by a space>          (only if cMod modules are bundled)
[basepath] +set fs_basepath "<basepath>"
[HM, masters cfg written OK] +exec vsp_masters.cfg
[HM, Loopback Host Fix on and router answered] +set cl_loopbackHostIP <public IPv4>
[SP, co-op Host chosen]    +set sp_coop 1 +set coop_backgroundPlay <0|1>
[SP, co-op Connect chosen] +set coop_connect "<a.b.c.d:port>" +set coop_sex <male|female> [+set coop_look <id>]
[SP, co-op either, name non-empty] +set coop_name "<CoopSafeName(name)>"
[HM, joinAddress from Browse…/link] [+set password "<pw>"] +connect <address>
<brightness args, or +exec vsp_brightness.cfg, or nothing for Custom>
```

Notes:

- `r_fbo 1` is always passed (MSAA needs it, and its gamma post-pass makes the
  Brightness sliders apply live; `L:1614-1616`).
- `cmodArgs` (`L:1461-1469`): `+set cl_cmodModules <1|0> +set cg_aspectCorrect <hudAspect>
  +set cg_aspectCorrectCenterHud <hudCenter> +set cg_predictWeapons <1|0>`
  (unquoted). Nothing at all when cMod modules aren't bundled.
- `VSP_LoopbackArgs()` (S:4525-4537): if the fix is on and the router hasn't
  answered yet, **pumps the event loop up to 1.5 s** waiting for it; returns
  nothing if still unknown.
- Brightness goes last so nothing else can be crowded out; its room is
  `size − (strlen(commandLine) + bytes appended so far)`.
- Log line `"Launcher: <args>"` with the join password replaced by `••••`
  (`L:1644`). The co-op password never goes on the command line (§6.2).
- Single player gets no `name` and no cMod args (they're Holomatch-only).
- The engine's command line after the launcher (macOS): `+set com_homepath
  EliteForce-MacOS <launcher args> <user argv>`.

### 3.3 Holomatch client args for the server window's "Play on This Mac" (`clientArgs`, `L:1583-1600`)

Built at start for `VSP_RunServerWindow` (S). An argv-style array (not a string):

```
+set com_holomatch 1  +set r_fullscreen <f>  +set r_mode -1  +set r_customwidth <w>
+set r_customheight <h>  +set r_highdpi <d>  +set r_picmip <p>  +set r_swapInterval <v>
+set r_fbo 1  +set r_ext_multisample <m>  +set name <CleanName>
<BrightnessArgs(b, room=600)>  <cmodArgs>  [+set fs_basepath <bp>]
[+exec vsp_masters.cfg]  [+set cl_loopbackHostIP <ip>]
```

(Order differs from appendTo: brightness before cMod; graphics are the
Holomatch ones since Host mode loads hmconfig.)

### 3.4 Mode choice

Mode = the selected card at Play time; Host never reaches the engine in this
process. `com_holomatch` 0/1 is the engine's switch. A dedicated server
(`+set dedicated 1|2`, `sys_headless`) never shows a launcher and always gets
`+set com_holomatch 1` appended after argv (`M:997-998`).

### 3.5 Join from Browse… / link

`joinAddress:password:` (`L:1237-1242`) stores the address and password and
selects Holomatch; the caller then presses Play. The address is passed raw after
`+connect` (link addresses are validated strictly, §7.1; browser addresses come
from the server list).

### 3.9 Start Server (Host mode Play) (`L:1376-1447`)

1. Save `LauncherMode = 2`, save server settings, flush defaults.
2. `running = VSP_RunningServers()` (S; servers run by app instances on this
   Mac: pid, name, port; oldest first). If non-empty, ask:
   - Message: 1 → "A server is already running on this Mac."; N → "N servers are already running on this Mac."
   - Informative: `"“<name or Server>” is running on port <port>. Another server would start on the next free port, with the settings in Config…; players would see both in the server list."` (about the **last** one)
   - Buttons: "Show Running Server" | "Start Another" | "Cancel" (Esc).
   - Show Running Server: activate that instance (all windows), log
     `Launcher: showing the running server (pid N)`, and stop. Cancel: stop.
     Start Another: continue.
3. Launch a **new instance of the app** with the single argument
   `--host-server`, the current environment minus every `VOYAGERSP_TEST_*`
   variable, activated. (macOS: LaunchServices "create new instance".) Log
   "Launcher: starting the server in its own window".
   Port: `CreateProcessW(<own exe>, "--host-server")` / `posix_spawn(<own exe:
   /proc/self/exe or $APPIMAGE>, "--host-server")`, detached.
4. Launch error → sheet alert "The server couldn't be started." with the
   system error text. Success → log "Launcher: server window started, pid N".

The launcher stays open; the user can still Play single player etc.

---

## 4. Game data (sys_osx_data.m, sys_osx_gog.c) vs sys_data.c

### 4.1 Lookup (`VSP_FindGameData`, `D:859-902`), no UI

`*outBasepath` = the fs_basepath to pass (nil = default/homepath copy);
`*outBase` = the BaseEF folder found. Order:

a) **Remembered in-place folder**: NSUserDefaults `GameFolder` (the folder that
   *holds* BaseEF). If `BaseIn(folder)` exists and has `pak0.pk3` → basepath =
   folder. Else log `Game data: remembered game folder "<f>" no longer has
   baseEF/pak0.pk3` and continue (the key is not cleared).
b) **Homepath copy**: `<HomeBase>/pak0.pk3` → basepath nil.
c) **Next to the app**: `<dir containing the .app>/BaseEF/pak0.pk3`, unless the
   app is translocated (path contains `/AppTranslocation/`, or
   `SecTranslocateIsTranslocatedURL` says so) → basepath = that dir.

Name matching (`ChildCI`, `D:157-169`): exact name first, else a
case-insensitive directory scan. `BaseIn(x)` = child `baseEF` (any case);
`Pak0In(x)` = child `pak0.pk3` (any case).

Logs: `Game data: remembered game folder "<base>"`, `Game data: "<HomeBase>"`,
`Game data: next to the app, "<base>"`, `Game data: app is translocated (<path>), not looking next to it`.

**Without the launcher** (`Sys_MacResolveGameData`, `D:1143-1183`): overridden
→ NULL; lookup succeeds → basepath (or NULL); else log `Game data not found (no
baseEF/pak0.pk3 in the remembered folder, "<HomeBase>" or next to the app)`,
init SDL video, run the chooser (§4.2); cancel → `exit(0)`; result basepath or
NULL. **Headless** (`O:295-320`): same lookup, no chooser; failure → stderr
`ERROR: no Elite Force game data: baseEF/pak0.pk3 was not found in the game folder chosen in the launcher, in "<HomeBase>" or next to the app. Run the game once to choose it, or pass +set fs_basepath <folder holding baseEF>.` and `exit(1)`.

### 4.2 Chooser ("Choose…"/"Change…", first run, File > Change Game Folder…) (`RunChooser`, `D:606-815`)

Loops until a valid choice or cancel. Returns NO on cancel; on success either
basepath is set (in place) or files were copied/unpacked to HomeBase (basepath
nil).

**Open panel** (`D:719-751`):

- Can choose directories **and** files; allowed file type `exe`; single
  selection.
- Prompt (OK button): "Choose". Message: "Choose your Elite Force folder, or GOG's installer".
- Accessory view (always disclosed; fixed text width 480, centered, insets 12/20):
  1. Small-font wrapping text: "Select the folder where Star Trek: Voyager – Elite Force is installed (the one containing BaseEF with pak0.pk3), the BaseEF folder itself, or GOG's Windows installer (the setup .exe). By default the game files are copied into the app's own folder in Application Support, so the original folder or CD is no longer needed."
  2. Checkbox "Use BaseEF files in place rather than copy" (default off, not remembered).
  3. Status line (small, secondary), live with the checkbox: on → "Files will be read from this folder; nothing is copied."; off → "Files will be copied into the app's data folder (about 600 MB for a standard install)."
  (Port: on Windows an `IFileOpenDialog` with `IFileDialogCustomize` (checkbox +
  static text); on Qt a non-native `QFileDialog` with extra widgets, or a small
  pre-dialog with the checkbox. A native dialog rarely allows folder **or** file
  in one go: two buttons ("Choose Folder…", "Choose GOG Installer…") are an
  acceptable equivalent.)
- Cancel → log "Game folder selection cancelled", return NO.

**Validation** of the chosen path (`D:754-814`):

1. **Not a directory and extension `.exe`** (any case): GOG installer →
   `UnpackInstaller(exe, onlyNew=NO)` (§4.3). Success (≥1 file and HomeBase has
   pak0) → remove `GameFolder`, return YES. Failure → back to the panel (errors
   were already shown). GOG is never "in place".
2. If the path has a `BaseEF` child directory: base = it; if no `pak0.pk3` in
   it → problem `MSG_PAK0_ON_CD`.
3. Else if the path itself has `pak0.pk3`, or its own name is `BaseEF` (any
   case): base = path; no pak0 → `MSG_PAK0_ON_CD`.
4. Else problem: "This folder has no BaseEF folder. Choose the Elite Force installation folder (the one containing BaseEF), or the BaseEF folder itself."
5. If no problem and **in place** and base's name isn't `BaseEF` (any case):
   "To use the files in place the folder must be called BaseEF. Rename it, or untick the checkbox to copy the files instead."
6. In place, OK: `GameFolder = parent of base`, return YES with basepath =
   parent. Log `Using game files in place from "<base>" (remembered)`.
7. Copy: `CopyGameFiles(base)` (§4.4); success → remove `GameFolder`, return
   YES. Failure → loop.
8. Problem → log `Rejected "<chosen>": <problem>`; alert "That folder can't be
   used" with `"<chosen path>\n\n<problem>"`; loop.

`MSG_PAK0_ON_CD` (`D:100`): "pak0.pk3 is missing — minimum CD installs leave it on the CD; copy it from BaseEF on the CD into this BaseEF folder, or choose a full installation."

`MSG_USE_BASEEF` (`D:99`): "Install the game on a Windows PC instead (from GOG, or the CD with the 1.2 patch), copy its BaseEF folder to this Mac, and choose that folder." (Port: reword on Windows/Linux, e.g. "Run GOG's installer (with Wine on Linux) instead, then choose the folder it installed to.")

Alerts in the data code (`Alert`, `D:205-231`): warning style unless stated,
given buttons; first button is index 0; log `[dialog] <title> — <text>`; with
`VOYAGERSP_TEST_CONFIRM` the first button is taken without showing.

### 4.3 GOG installer unpack (`UnpackInstaller`, `D:532-600`; parser `G`)

GOG sells the game as an Inno Setup 5 Windows installer (`setup_….exe`). This
code is **already portable C** (`G:35-37`: CommonCrypto on macOS, bundled
public-domain `sys_digest.h` SHA-1/MD5 elsewhere; liblzma; UTF-8 paths with
wide-char file APIs on Windows). Built when `EF_GOG` is on (default ON on
Apple, OFF elsewhere: "no caller until M2"; `engine/CMakeLists.txt:35-40`; on
Windows a static liblzma from the deps prefix is already wired, `:340`).

API (`GH`):

```c
typedef struct { uint64_t offset, stored, size; uint8_t sha1[20], md5[16]; } gogPart_t;
typedef struct { char name[64]; uint64_t size; uint8_t md5[16]; int numParts, partsFound; gogPart_t *parts; } gogFile_t;
typedef struct { gogFile_t *files; int numFiles; } gogInstaller_t;
int  GOG_ReadInstaller(const char *path, gogInstaller_t *inst, char *err, size_t errSize);  // 0 + err phrase on failure
void GOG_Free(gogInstaller_t *inst);
int  GOG_ExtractFile(const char *installer, const gogFile_t *f, const char *to,
        volatile unsigned long long *progress, volatile signed char *cancel, char *err, size_t errSize);
```

Format and algorithm (`G:7-31`, `G:369-503`):

1. Read up to the first 8 MB; must start with `MZ` ("it isn't a Windows installer").
2. Find the loader table magic `"rDlPtS\xcd\xe6\xd7\x7b\x0b\x2a"` (12 bytes); take 40 bytes;
   `LE32(+12)` must be 1; `Offset0 = LE32(+32)` (setup data), `Offset1 = LE32(+36)` (file data).
3. At Offset0: 64-byte ID starting `"Inno Setup Setup Data (5."` and containing `") (u)"` (Unicode build).
4. Two blocks follow at Offset0+64 (`ReadBlock`, `G:212-286`): 9-byte header
   `CRC32(4) | size(4) | compressed(1)`, CRC32 over bytes 4..8 (zlib `crc32`);
   compressed must be 1; data = `size` bytes in pieces of `CRC32(4) + up to 4096
   bytes`, each CRC-checked; joined data = 5 LZMA1 property bytes + raw LZMA1
   stream, decoded with liblzma `lzma_properties_decode` + `lzma_raw_decoder`
   (filter `LZMA_FILTER_LZMA1`); output buffer grows up to 64 MB. Block 1 =
   setup header (entries), block 2 = file locations (74 bytes each).
5. Scan block 1 for the UTF-16LE text `after_install(`; the string starts 4 bytes
   earlier (4-byte length + UTF-16LE, ASCII only). Parse
   `after_install('<32 hex md5 of part>', <stored>, <size>)`, then the next
   string `before_install('<32 hex md5 of file>', '<path>', <parts>)` (empty for
   2nd+ parts; parts 1…4096), then skip 20 bytes (MinVersion,
   OnlyBelowVersion) → `LE32` location index.
6. Keep files whose path is `BaseEF\<name>.pk3` (no deeper folder, any case;
   name < 64 chars) — pak0-pak3 (so the 1.2 patch and Expansion Pack) and
   playermaps.pk3. Each location (74 bytes) gives: start offset `LE32(+8)` —
   part data at `Offset1 + start + 4` (skipping GOG's `zlb\x1a` chunk id),
   SHA-1 at `+36`; checks `LE32(+0)==LE32(+4)` (same disk), `LE64(+12)==0`,
   `LE64(+20)==LE64(+28)==stored` (not Inno-compressed), within file size.
7. All files must have all parts; at least one file.

Extraction (`G:505-621`): to `<to>.part`; per part: read stored bytes in 1 MB
chunks, SHA-1 them, `inflate` (**zlib format, with header**, `inflateInit`),
write output, MD5 per part and per whole file; check stored SHA-1, part MD5,
`Z_STREAM_END`, exact size; then whole-file MD5; close; rename to `<to>`
(Windows: `MoveFileExW(MOVEFILE_REPLACE_EXISTING)`); `*progress += bytes
written`; `*cancel` checked per chunk ("Cancelled."). Any failure deletes the
`.part`. Uses static 1 MB buffers: **not re-entrant** (one extraction at a time).

Error phrases (shown after "This app can't unpack it: "): "it can't be read
(<errno text>)", "it isn't a Windows installer", "it isn't an Inno Setup installer
like GOG's", "it's an Inno Setup version this app doesn't know, or the download is
incomplete", "it's an Inno Setup version this app doesn't know (<id>)", "its setup
data is damaged (block header)", "its setup data is stored in a way this app doesn't
know", "its setup data can't be read", "its setup data is damaged (checksum)", "its
setup data isn't compressed the way this app expects", "the system's LZMA decoder
couldn't start", "its setup data couldn't be decompressed", "its file list isn't
laid out the way this app expects", "its file list points outside itself", "<name>
is stored in a way this app doesn't know, or the download is incomplete", "<name> is
incomplete in its file list", "it has no Elite Force game files (BaseEF\*.pk3) that
this app can find". Extraction errors: "Can't read the installer: …", "Can't write
<path>.part: …", "zlib couldn't start", "<name> is damaged in the installer (part i
of n)", "<name> doesn't match its checksum (part i of n)", "<name> doesn't match its
checksum", "Can't rename …", "Cancelled.".

Launcher side (`UnpackInstaller`, `D:541-600`):

1. Log `GOG installer: reading "<exe>"`. Parse failure → log and alert **"This
   installer can't be used"**: `"<exe file name>\n\nThis app can't unpack it:
   <phrase>.\n\n<MSG_USE_BASEEF>"` → return −1.
2. For each file: with `onlyNew` (Add Game Files), skip it if HomeBase or the
   remembered external BaseEF has a file of the same name (any case) and the
   same size. Destination: existing same-name file in HomeBase (any case) or
   `HomeBase/<name>`.
3. Log `GOG installer: N .pk3 files (<size> to unpack), K already present`.
4. Nothing to do → alert "Nothing to add" / "The game already has all of this
   installer's .pk3 files." → return 0.
5. Free space check (§4.4) → "Not enough disk space" / "Unpacking the game files
   needs <total+64 MB>, but only <free> is free." → −1.
6. Run the copy job (progress panel titled "Unpacking the GOG installer"). Fail →
   −1. Log `GOG installer: unpacked <names> (<size>) to "<HomeBase>"`. Return
   count.

### 4.4 Copying (chooser copy, Add Game Files) and the progress panel

**CopyGameFiles(srcBase)** (`D:452-527`):

- Sources: top-level entries of srcBase not starting with `.`, extension `pk3`
  (any case), regular files after resolving symlinks → `HomeBase/<same name>`.
  (Nothing else: no .tmp/.bsp/.map/.cfg/.log.)
- Plus `saves/` (any case) recursively: regular files not starting with `.`;
  **existing destination saves are never overwritten** (counted "kept") →
  `HomeBase/saves/<rel>`.
- Log `Copying N .pk3 files and M saves (K existing saves kept) from "<src>" to "<dst>": <size>`.
- Free space: create HomeBase; if free is known and `< total + 64 MiB` → alert
  "Not enough disk space": `"Copying the game files needs <total+64MB>, but only <free> is free on the disk containing\n<HomeBase>.\n\nFree up some space, or tick \"Use BaseEF files in place rather than copy\" to use the game folder directly."`
- Run the copy job. Then HomeBase must have pak0 else alert "Copying the game
  files failed" / "pak0.pk3 did not end up in the destination folder.".
- Log `Copied <size> of game files to "<HomeBase>"`.

Sizes are formatted like the OS file-size formatter (`NSByteCountFormatter`,
file style: decimal units, e.g. "600 MB"). Free space = "available for important
usage" (falls back to plain free size); 0 = unknown (no check).

**Add Game Files…** (`VSP_AddGameFiles`, `D:921-1040`):

- Open panel: files and folders, **multiple selection**, prompt "Add", message
  "Choose .pk3 files, a folder that contains them (such as BaseEF of the Expansion Pack or its CD), or GOG's installer (setup_….exe)". Cancel → 0.
- `external` = BaseEF of the remembered in-place folder, if any.
- Every picked `.exe` file → `UnpackInstaller(path, onlyNew=YES, external)`;
  if only installers were picked, return their total.
- Every other pick: a directory → its BaseEF child if present, else itself; list
  its top-level entries; a file → itself. Keep: not dot-files, extension `pk3`,
  regular file (symlinks resolved), name not already queued (case-insensitive).
  Skip ("present") when HomeBase or external has the same name (any case) with
  the **same size**; a same-name file of different size is **replaced** in place
  (destination keeps the existing file's spelling).
- Log `Add game files: N new .pk3 (<size>), K already present`.
- Nothing → alert "Nothing to add": K==1 "The game already has that .pk3 file.";
  K>1 "The game already has all of those .pk3 files."; K==0 "No .pk3 files were
  found there. Choose .pk3 files, or the BaseEF folder that contains them." → 0.
- Free space `< total + 64 MiB` → "Not enough disk space" / "Adding the files
  needs <total>, but only <free> is free." → 0.
- Copy job; log `Added <names> (<size>) to "<HomeBase>"`; return count. The
  launcher logs "Launcher: game files added" if > 0, refreshes, re-fronts.
- Files always go to HomeBase (which the engine always searches), even when the
  game data is used in place.

**Copy job** (`RunCopyJob`, `D:369-437`; worker `D:331-367`; per-file copy
`D:268-329`):

- Panel: 440×120, titled, not closable; title "Copying game files" or
  "Unpacking the GOG installer" (if the first source is a GOG item). Label
  (truncates middle) `"Copying game files to <HomeBase> …"` / `"Unpacking game
  files to <HomeBase> …"`; determinate progress bar 0…total bytes; "Cancel"
  button. App-modal; the UI polls the worker every 20 ms.
- Worker thread: for each item create the parent dir; GOG item →
  `GOG_ExtractFile`; plain file → copy in 1 MB blocks to `<to>.part`, honoring
  cancel, preserve atime/mtime (the save menu shows dates), then
  `rename(.part, to)` (a running engine keeps reading the old inode).
- Failure or cancel: delete every destination file **created** by this job
  (files that existed before stay). Errors: "Cannot read <p>: <errno>",
  "Cannot write <p>.part: <errno>", "Cannot rename …", "Cancelled.". Log
  `Copy failed: <error>`.
- After a failure that is not a cancel: GOG → alert "Unpacking the GOG
  installer failed": `"<error>.\n\nThe partly unpacked files were removed. If the download is damaged, download it again from GOG; otherwise: <MSG_USE_BASEEF>"`;
  plain → "Copying the game files failed": `"<error>\n\nThe partial copy was removed."`.
  Cancel: no alert.

### 4.5 Expansion Pack and patch checks

- `VSP_HasExpansion(base)` (`D:1042-1117`): true if `maps/tour/deck02.bsp`
  (Virtual Voyager's first map; the same test as the SP main menu) exists loose,
  or as an entry (case-insensitive name) of any top-level `*.pk3` (not
  dot-files), in HomeBase or base. The zip check (`ZipHasEntry`, `D:1051-1098`)
  reads only the End-Of-Central-Directory record (signature `PK\5\6`, searched
  backwards in the last 64 KiB+22; cd size at +12, cd offset at +16) and the
  central directory (entries `PK\1\2`, name length at +28, extra +30, comment
  +32, name at +46) — no zone allocation, usable before `Com_Init`. Portable as
  is (replace NSFileHandle with stdio).
- `VSP_MissingPatchPaks(base)` (`D:1119-1136`): `pak1.pk3` and `pak2.pk3`
  (patch 1.1 and 1.2, which also carry Holomatch's game code; the engine's
  `FS_CheckPak0` insists on them), missing from both HomeBase and base (any case).

### 4.6 What sys_data.c (Windows/Linux) already covers vs. what's missing

| Feature | macOS | sys_data.c |
|---|---|---|
| Overridden-by-command-line test (same 7 names) | `VSP_DataOverridden` | `Sys_DataOverridden` ✔ |
| a) remembered folder | NSUserDefaults `GameFolder` | `<homepath>/launcher.cfg` line `gamefolder "<folder holding BaseEF>"` (whitespace-tolerant, optional quotes, last line wins; `SD:486-517`). **Nothing writes it yet: the launcher must.** Accepts the folder holding BaseEF **or** BaseEF itself (`GameFolder()` `SD:411-430`); macOS only the former |
| b) homepath copy | ✔ | ✔ (returns NULL = default basepath) |
| c) next to the app | ✔ (skips translocated) | next to the exe (`SDL_GetBasePath`, else argv[0] dir); AppImage: next to `$APPIMAGE` ✔ |
| d) well-known installs | ✗ | ✔ Windows: GOG registry `HKLM\SOFTWARE\[WOW6432Node\]GOG.com\Games\*` value `path`; Uninstall entries (HKLM both views + HKCU) whose `DisplayName` contains "Elite Force" (`InstallLocation`); dir globs under `%SystemDrive%\`, `%ProgramFiles(x86)%`, `%ProgramFiles%` (`GOG Galaxy/Games/*`, `GOG Games/*`, `Raven/*`, `Activision/*`, `Raven Software/*`, three literal names). Linux: Heroic (incl. Flatpak, Steam Deck SD card), Wine/Lutris/Bottles/Heroic/Proton prefixes' `drive_c` (`SD:566-663`) |
| BaseEF named differently on Linux | n/a (case-insensitive FS) | symlink `<homepath>/.gamedata/baseEF → found` and use `.gamedata` as fs_basepath (`SD:678-706`) — **an in-place choice on Linux must go through this too** |
| Not found | chooser UI | error message box (SDL) "Elite Force: game files not found" with instructions, `exit(1)` (`SD:752-779`) |
| Early log replay | `MacLog` + `Sys_MacDataPostInit` | `DataLog` + `Sys_DataPrintEarlyLog` ✔ (8 KB buffer) |
| Chooser (folder / BaseEF / GOG exe), in-place checkbox, validation messages | ✔ | ✗ missing |
| Copy into homepath with progress, cancel, rollback, saves merge, free-space check | ✔ | ✗ missing |
| GOG installer unpack | ✔ (`sys_osx_gog.c`, portable) | ✗ not wired (EF_GOG off) |
| Add Game Files… | ✔ | ✗ missing |
| Expansion detection, missing-patch detection | ✔ | ✗ missing (pure C candidates) |
| Show folder in file manager | ✔ | ✗ missing |
| Report status/basepath to a UI without exiting | `VSP_FindGameData` returns BOOL | `Sys_DataResolve` exits on failure — **needs a non-exiting variant** returning found/basepath/base and the "how found" string |
| File menu / relaunch / delete settings | ✔ | ✗ missing |

Semantic note: macOS reports basepath non-nil for "next to the app" as well as
"in place"; the status line says "using the files in this folder" for both.
sys_data.c's (c) and (d) results are in place by nature.

---

## 5. In-game platform features tied to the launcher (File menu etc.)

### 5.1 Quit-to-launcher and relaunch (`D:1185-1267`)

State: `fromLauncher` (set by `Sys_MacSetFromLauncher` after Play),
`pendingRelaunch` (−2 none; −1 launcher; 0 SP; 1 Holomatch), `pendingDelete`.

- `Sys_MacQuitToLauncher` (from `Com_Quit_f` on a bare `quit`): if
  `fromLauncher` and nothing pending and no quit signal (SIGTERM/SIGINT, e.g.
  logout or kill) → `pendingRelaunch = −1` and register `DoRelaunch` with `atexit`.
- `RelaunchIn(mode)`: register `DoRelaunch` (once), set `pendingRelaunch =
  mode`, queue `quit`.
- `DoRelaunch` runs at process exit (after the engine wrote its config and
  removed its PID file): `open -n -a <bundle>` (launcher) or `open -n -a
  <bundle> --args +set com_holomatch <0|1>`; waits for `open` to return. Logs
  `Relaunching: open -n -a "<bundle>"[ --args +set com_holomatch N]` on stderr.
  `VOYAGERSP_TEST_NORELAUNCH` → log only.
  Port: at exit, spawn `<own exe>` (no args → launcher; or `+set com_holomatch
  N`) detached, with the same environment; don't wait for it. A relaunch with
  `com_holomatch` skips the launcher, so in that instance `fromLauncher` stays
  false and its Quit really quits. Only the mode is passed; graphics etc. come
  from the cfg files the engine just wrote.

### 5.2 File menu (`D:1520-1553`, inserted by `Sys_MacFrame` `D:1566-1615`)

Top-level "File" menu inserted right after the application menu, checked every
16th frame (re-added if SDL rebuilt the menu bar; logged `[skipnotify]macOS:
added the File menu (…)`). Items (no key equivalents), in order:

1. "Return to Launcher…" → informational alert "Return to the launcher?":
   HM "This leaves the current game and restarts Elite Force at the launcher."
   / SP "This restarts Elite Force at the launcher. Progress since your last save is lost."
   Buttons "Return to Launcher" | "Cancel" → `RelaunchIn(−1)`.
2. "Switch to Single Player" (in HM) / "Switch to Holomatch" (in SP) → alert
   title "Switch to Single Player?" / "Switch to Holomatch?", text HM "This leaves
   the current game and restarts Elite Force in single player." / SP "This restarts
   Elite Force in Holomatch. Progress since your last save is lost.", buttons
   "Switch" | "Cancel" → `RelaunchIn(other mode)`.
3. — separator —
4. "Show Saves Folder" → create `<HomeBase>/saves` if needed and open it.
5. "Show Game Folder" → open the BaseEF in use (fs_basepath's BaseEF if it has
   pak0, else HomeBase if it has pak0) or alert "No game folder" / "No BaseEF
   folder with pak0.pk3 is in use."
6. — separator —
7. "Change Game Folder…" → the chooser (§4.2); on success alert "Game folder
   changed" / "The new game folder will be used the next time you start the
   game." buttons "Restart Now" | "Later" → Restart Now = `RelaunchIn(current mode)`.
8. — separator —
9. "Delete Saves & Settings…" (§5.4).

Console commands with the same actions (`D:1472-1517`, bundle only):
`macos_launcher`, `macos_switchmode`, `macos_showsaves`, `macos_showgamefolder`,
`macos_changegamefolder`, `macos_deletesettings`; plus diagnostics
`macos_windowstate` and cvar `macos_debugpointer` (CVAR_TEMP; logs pointer/menu
state every 30 frames). Port: platform-neutral names (e.g. `launcher`,
`switchmode`, `showsaves`, `showgamefolder`, `changegamefolder`,
`deletesettings`), optionally keeping `macos_*` as aliases. Windows/Linux have
no global menu bar: these commands (and possibly an in-game menu entry or the
window's system menu) are the equivalent of the File menu.

While any menu-bar menu is open the sound output is paused (`D:1496-1507`).

### 5.3 Close-window confirmation (`Sys_MacConfirmQuit`, `D:1617-1699`)

Called from `sdl_input.c:2306-2316` on `SDL_WINDOWEVENT_CLOSE` (close button /
Cmd+W) — macOS only today (IN_Init stops SDL turning these into SDL_QUIT). A
sheet on the game window: title `"Quit <app name>?"` (fallback "Elite Force");
SP adds "Progress since your last save is lost."; buttons "Quit" (Return) |
"Cancel" (Esc). SP: the game waits (modal, sound paused); HM keeps running.
Quit → `quit Closed window` (no return to launcher). Cancel → keyboard back to
the game window. Only one at a time. `VOYAGERSP_TEST_NODIALOG` or no window →
quit at once. Cmd+Q / app menu Quit → quits at once without asking. Port: a
modal message box on Windows/Linux close (Alt+F4 / WM close) for parity.

### 5.4 Delete Saves & Settings… (`D:1338-1442`)

- `SafeHomeRoot`: the homepath only if it is a real directory (lstat, **not a
  symlink**), directly inside its parent (realpath of the parent + `/` + name),
  and its on-disk name equals `EliteForce-MacOS` case-insensitively; else nil.
- nil → alert "Nothing to delete" / `"<homepath> does not exist, or is a link to another location, so nothing was deleted."`
- Else count files/bytes (regular files recursively), files under
  `baseEF/saves`, and `*.pk3` in `baseEF`; critical alert "Delete saves &
  settings?" with:

  ```
  This quits the game and moves this folder to the Trash:
  <homepath>

  • saved games (<n> files)
  • settings and key bindings (efspconfig.cfg, hmconfig.cfg, keys, console history)
  [• game files copied by the app (<k> .pk3, <size>)]

  Total: <files> files, <size>.

  <Your game folder at <ext> will not be touched. | Nothing outside this folder is touched.>
  ```
  ("ext" = the BaseEF in use if it is outside the homepath.) Buttons "Move to
  Trash and Quit" (destructive) | "Cancel".
- Confirm: remove `GameFolder` default, set `pendingDelete`,
  `atexit(DoPendingDelete)`, queue `quit` (a pending delete also blocks the
  quit-to-launcher relaunch).
- At exit: re-check SafeHomeRoot (refuse with a stderr line otherwise); move it
  to the Trash; stderr `Moved "<p>" to the Trash`; on error stderr + alert
  "Could not delete saves & settings" / `"<p> could not be moved to the Trash: <err>\n\nYou can delete it in Finder."`.
  `VOYAGERSP_TEST_NOTRASH` → log only. Other NSUserDefaults keys are **not**
  cleared. It never touches a user-chosen game folder.
  Port: Windows `IFileOperation` with `FOFX_RECYCLEONDELETE` (or
  `SHFileOperationW` + `FOF_ALLOWUNDO`), "Recycle Bin" wording; Linux
  freedesktop Trash (`QFile::moveToTrash` / `gio trash`). With launcher.cfg as
  the settings store, `gamefolder` disappears with the folder.

### 5.5 Homepath setup (`O:433-543`) and bundle environment

- Homepath `~/Library/Application Support/EliteForce-MacOS` (`EF_HOMEPATH_NAME`).
  A folder whose name matches case-insensitively, or the legacy name
  `VoyagerSP-MacOS`, is renamed (via a temporary name) to the current name; on
  failure it's used as is. If `<homepath>/baseEF` doesn't exist but
  `~/Library/Application Support/<HOMEPATH_NAME, "Lilium Voyager">/baseEF` does,
  **copy** it once (never move), removing a stale `<PRODUCT_NAME>.pid`. Messages
  are replayed into the console. Skipped when argv names
  `fs_homepath`/`com_homepath`. (Windows/Linux: no legacy migration today.)
- macOS-only: bundled Vulkan loader/MoltenVK env (`O:380-407`), press-and-hold
  default, SIGTERM/SIGINT via dispatch sources.

---

## 6. Co-op (EF_COOP builds only)

Build flag `EF_COOP` (default OFF, "experimental"; `CMakeLists.txt:21`).
Without it the launcher has no Co-op… button and appends nothing
(`CH:43-49`). Currently the engine side is **macOS only**
(`engine/CMakeLists.txt:249`). Two parts:

- `sys_osx_coop.m`: the launcher's **Co-op sheet** *and* engine-side code (the
  host's `getcoopinfo` responder, `Sys_CoopFrame`, background play, `Sys_CoopMacName`).
- `sys_coop_net.c`: the host↔guest connection (almost portable C).

### 6.1 Co-op sheet UI (`C:788-1314`)

Opened by the Single Player card's "Co-op…" button (only if no other sheet is
attached; `C:1327-1343`). Sheet window, content width 560, title "Co-op",
insets 18/20/20/20, vertical spacing 12 (4 after the heading, 18 after the Join
group). Initial focus: the Join address field.

1. Heading row: "Co-op" (17 pt bold) + "EXPERIMENTAL" (10 pt semibold, orange).
2. Intro (12 pt secondary, wrap 520): "Play the campaign with a friend. The host is player 1 and plays the story; the friend joins as player 2."
3. **Name row** (`C:895-918`): "Your name" (12 pt) | text field width 220 | hint
   "up to 31 letters" (11 pt secondary). Initial: `CoopSafeName(saved
   CoopPlayerName)` if non-empty else the computer's name (cleaned; "Mac" if
   empty). Placeholder: the computer name. Tooltip: "Shown to the other player in co-op messages, such as “Hold Use to revive …”".
4. **HOST group** (`C:920-985`), caption "HOST". Left column (spacing 8),
   right a "Host" button (min width 80, top-aligned):
   - "Starts single player with co-op on: choose New Game or Load as usual. Your friend then joins as player 2 with this Mac’s address:" (12 pt, wrap 400)
   - LAN address list: one line per interface `"<IPv4>    (<ifname>)"` (4 spaces), 13 pt monospaced medium, selectable; or (orange, 12 pt) "No network found. Connect this Mac to Wi-Fi or Ethernet first."
     Addresses: every interface that is up and running, IPv4, not loopback,
     not 169.254/16; `en0` first (`C:578-607`). (Port: the interface with the
     default route first; Windows `GetAdaptersAddresses`, show the adapter's
     friendly name.)
   - "Password (optional)" (12 pt) + secure field width 160, placeholder "none"
   - checkbox "Keep playing in the background" (initial from `CoopKeepPlayingInBackground`, default on); tooltip "The game goes on while its window isn’t in front, e.g. while you click into a second copy on this Mac (keys and the mouse still go only to the window in front)"
   - checkbox "Open the port on my router (for a friend on the internet)" (state mirrors the NAT mapping: on while asking or mapped)
   - internet status (11 pt, wrap 400, selectable), see below
   - "Copy Invite" (small) — tooltip "Copy a stvef://coop/ link for your friend: opening it shows Co-op with your address filled in"; enabled iff an invite link exists and the router isn't being asked
   - note (11 pt secondary): "Co-op uses UDP port 29760. macOS may ask whether Elite Force can find devices on your local network: allow it. Player 2 follows you until your friend joins; then your friend plays player 2 and sees your game from player 2’s eyes."
   - "Host" button: enabled iff the launcher can play (data present, no missing patch); tooltip "Play single player with co-op on" / "Add the game files first".
5. **JOIN group** (`C:987-1048`), caption "JOIN", column spacing 12:
   - Row: "Host’s address" (12 pt) | text field (min width 260; placeholder "192.168.1.20, or address:port (default 29760)"; **Return = Check**) | "Check" button (min width 80; tooltip "Ask that address whether a co-op game runs there").
   - Row (spacing 24): ["Password" + secure field 160, placeholder "if the host set one"] ["Munro" + segmented "Male" | "Female" (initial from `CoopGuestMunro`, default Male); tooltip "How player 2 looks on both screens. The story still speaks to the host’s Munro."]
   - Row: "Character" (12 pt) | popup: "Munro (default)", then a separator, a disabled "Voyager crew" header and the crew, a separator, a disabled "Hazard Team" header and its members (indented one level; titles and ids from `efgame/src/game/coop_characters.h`, the same table the game checks); initial from `CoopGuestCharacter` (default Munro) | "with the voice of the Munro above" (11 pt secondary).
   - Result row (hidden until the first Check): icon (SF symbol 20 pt, width 26) or a small spinner while checking; title (13 pt semibold, wrap 440); text (12 pt secondary, wrap 440, selectable; hidden when empty).
   - Bottom row: "Connect" (min width 80; disabled until a Check found a game; tooltip "Check the address first" / `"Join <address> as player 2"`) + text (11 pt secondary, wrap 380): "Connect starts single player and joins the host’s game as player 2: you see it from player 2’s eyes and play player 2 while the host plays the story."
6. Footer: right-aligned "Cancel" (Esc, min width 80).

**Internet status text** (`showInternet`, `C:1095-1122`):

| State | Text | Color |
|---|---|---|
| mapped (`coopNatPublic`) | `"Port open: a friend on the internet joins <ip:port>. Copy Invite and send it to them. The port stays open while Elite Force runs."` | green |
| asking | "Asking your router to open the port…" | secondary |
| failed | `"Port not opened. <reason> Forward UDP port 29760 to this Mac (<first LAN IP, or “this Mac’s address”>) in your router’s settings instead, or use Tailscale (see the README)."` | orange |
| idle | "Not needed on the same network. Copy Invite gives your friend a link to this Mac’s address." | secondary |

After Copy Invite: `"Copied <link>. Your friend opens it (Elite Force for Mac with co-op) to check your game and join."`, log `Co-op: invite <link>`.

**NAT mapping** (`C:609-746`): checkbox on → `DNSServiceNATPortMappingCreate`
(UDP, internal = external = 29760, default lease, renewed by mDNSResponder
until released) with a 15 s give-up timer. Results / reasons:

- unsupported: "Your router doesn’t support automatic port opening (NAT-PMP / UPnP), or has it turned off."
- disabled: "Automatic port opening (NAT-PMP / UPnP) is turned off in your router’s settings."
- double NAT: "Your network has two routers in a row (double NAT), so the outer one still blocks the port."
- other error: `"The router didn’t open the port (error <n>)."`
- external address 0: wait for another callback.
- external address private (10/8, 172.16/12, 192.168/16, 100.64/10): `"Your router opened the port, but its own internet address (<ip>) is private: your provider or another router puts it behind a second NAT, which nobody outside can get through."`
- timeout 15 s: "Your router didn’t answer the request to open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned off."
- success: public = `"<ext ip>:<ext port or 29760>"`, log `Co-op: the router forwards <p> to this Mac`.

The mapping is process-wide; checkbox off → removed. Kept when Host is pressed
(the game opens its socket on the same port in the same process); removed on
Cancel, on Connect (a guest needs none), and implicitly when the app exits.
Port: libnatpmp + miniupnpc (the server spec needs the same for its "open port"
option and the Loopback Host Fix's address lookup); share one implementation.

**Invite link** (`C:748-762`): `stvef://coop/<public ip:port>` when mapped, else
`stvef://coop/<first LAN IPv4>:29760`; none when neither.

**Check** (`C:1227-1249`): if already checking, ignore. Disable Check, show the
spinner and `"Checking <address or “the address”>…"`; run `VSP_CoopQuery(address,
2.0)` on a background thread; on return re-enable Check, log `Co-op: check <a>
-> <result>`, show the result:

| status | Icon / color | Title | Text |
|---|---|---|---|
| found | `checkmark.circle.fill` green | `"Co-op game found[ on “<hostname>”] (<ip:port>)"` | description (below) |
| mismatch | `exclamationmark.triangle.fill` orange | `"Version mismatch: the co-op game[ on “<hostname>”] (<ip:port>) can’t be joined from this Mac"` | `<error>\n\n<description>` |
| none | `xmark.circle.fill` secondary | `"No co-op game found at <ip:port>"` | error, or "Nothing answered within 2 seconds. Check the address, that the host pressed Host and is running co-op, and that both Macs are on the same network." |
| bad | `exclamationmark.circle.fill` red | "This address can’t be used" | error |

Connect is enabled iff status found and the launcher can play. Editing the
address after a found result disables Connect again (tooltip back to "Check the
address first").

Description lines (`CoopDescribe`, `C:1162-1195`):

1. Not playing or no map → "The host is in the menus: no level is loaded yet."; with mission → `"Mission: “<mission>” (map <map>)"`; else `"Map: <map>"`.
2. `"Difficulty: <Easy|Medium|Hard|Skill N>"` (skill 0/1/2) + if `time` present: `" · on this level for under a minute"` (t<60) or `" · on this level for <t/60> min"`.
3. `"Players: <max(players,1)> of <maxPlayers or 2>"` + `guest==1` → " (the host, and a friend as player 2: the game is full)"; else players≥2 → " (the host, and player 2 as a stand-in on the host’s Mac until a friend joins)"; else " (the host)".
4. If `player` → `"Host: <CoopSafeName(player)>"`.
5. If `needPassword==1` → "The host set a password."
6. `"Host’s app: Elite Force for Mac <version or ?> · co-op version <coopProtocol or ?>"`.

**Host** (`C:1251-1269`): requires canPlay. End the sheet (OK); keep the NAT
mapping; remember Host chosen, background flag (saved to defaults), name
(`takeName`: cleaned name saved to `CoopPlayerName`; empty → computer name
used); `Sys_CoopSetLaunchPassword(host password)`; log `Co-op: hosting (single
player with sp_coop 1) as <name>[, with a password]`; select Single Player;
**Play**.

**Connect** (`C:1282-1307`): requires a found address and canPlay. If the host
needs a password and the field is empty → result shows `lock.fill` orange "This
game needs a password" / "Type the password the host gave you, then press
Connect." and focuses the password field. Else remove the NAT mapping, end the
sheet, remember the address, name, sex (saved), `Sys_CoopSetLaunchPassword(join
password)`, log `Co-op: joining <addr> as player 2 (single player with
coop_connect), <name>, <sex> Munro`, select Single Player, **Play**.

**Cancel**: end the sheet; NAT mapping removed (`C:1336-1341`).

### 6.2 Co-op launch arguments (`VSP_CoopAppendArgs`, `C:1442-1461`)

Only when not Holomatch:

- Host chosen: `+set sp_coop 1 +set coop_backgroundPlay <1|0> `
- Connect chosen and the address contains only `0-9 . :`:
  `+set coop_connect "<ip:port>" +set coop_sex <male|female> `, then `+set coop_look <id> ` when the
  chosen character's id is in `coop_characters.h` (Munro: nothing)
- Either, with a non-empty name: `+set coop_name "<CoopSafeName(name)>" `

The **password is never on the command line**: the launcher hands it to the
engine in-process via `Sys_CoopSetLaunchPassword()` (a 64-byte static buffer,
`N:162-174`); the `coop_password` cvar overrides it when set. Since the
Windows/Linux launchers also run in the game process, the same in-process call
works. (If a port ever spawns the game as a separate process, pass it through an
env var or pipe, never argv.)

**CoopSafeName** (`C:809-826`): transliterate to Latin then ASCII (ICU
`"Any-Latin; Latin-ASCII"`), keep 0x20…0x7E except `"` `\` `;` `%`, skip leading
and repeated spaces, at most 31 (`COOP_NAME_MAX`) characters, trim. Port: a
small diacritic-folding table (Latin-1 Supplement + Latin Extended-A), or
iconv `ASCII//TRANSLIT` on Linux; exact transliteration equality isn't required
for interop (the engine cleans names again, `CoopCleanName` `N:232-250`).

### 6.3 Engine side in sys_osx_coop.m (must move to portable C)

- **Cvars** registered on the first `Sys_CoopFrame` outside Holomatch/dedicated
  (`C:388-403`): `coop_connect` "" TEMP, `coop_password` "" TEMP, `coop_name`
  "" TEMP, `coop_sex` "" TEMP, `coop_look` "" TEMP, `coop_backgroundPlay` "1" ARCHIVE; plus
  `coop_netstats`, `coop_simlatency`, `coop_simloss` (TEMP) and commands
  `coop_status`, `coop_disconnect`, `coop_kick` (`N:1997-2008`). `sp_coop` is
  the game's latched cvar (1 = host, turns co-op on at the next level start;
  2 = guest replica level, set by the engine).
- **Sys_CoopFrame** every frame (`C:378-427`): run the guest
  (`CoopNet_GuestFrame`); if Holomatch, dedicated, `sp_coop != 1`, or this copy
  is a guest → close the host socket (telling a connected guest "the host
  stopped co-op") and return; else open the host socket (UDP, `INADDR_ANY`,
  port 29760 or `VOYAGERSP_TEST_COOP_PORT`, non-blocking; on bind failure print
  once "co-op: can't answer co-op checks, UDP port N is in use (…); trying again
  every 5 s" and retry every 5 s; on success "co-op: answering co-op checks on
  UDP port N"), read up to 16 packets per frame: a `getcoopinfo` query is
  answered, anything else goes to `CoopNet_HostPacket`; then `CoopNet_HostFrame`.
- **getcoopinfo answer** (`C:276-329`): request = `\xff\xff\xff\xff` +
  `getcoopinfo` + optional (space-separated) challenge of up to 32 chars from
  `[A-Za-z0-9_-]`. Rate limits (leaky bucket as the server's SVC_RateLimit):
  10/s per source address (32-slot table, oldest evicted) and 10 per 100 ms
  overall; limited queries are dropped silently. Reply: `\xff\xff\xff\xff`
  `coopInfoResponse\n` + infostring with keys `coopProtocol` (7), `version`
  (EF_APP_VERSION), `hostname` (computer name), `paks` (`FS_CoopPaks()`), `player` (coop_name, if set),
  `state` (`playing`|`menu`), when playing `map`, `mission` (cvar
  `coop_mission`), `time` (cvar `coop_time`), always `skill` (`g_spskill`),
  `players` (playing ? max(1, `coop_players`) : 1), `maxPlayers` 2,
  `needPassword` 0/1, `guest` 0/1, `challenge` (echoed). Values cleaned: `\ ; "`
  and control chars → space, ≤127 bytes, no split UTF-8 sequence at the end.
  (`coop_mission`/`coop_players`/`coop_time` are published by the game,
  `efgame/src/coop/coop_info.cpp`.)
- **Background play** (`C:331-376`): `Sys_CoopBackgroundPlay()` = 1 when
  `coop_backgroundPlay` is on, single player, not dedicated, and (guest active,
  or host socket open with `sp_coop 1`). Then `Com_Frame` skips the
  unfocused/minimized fps caps, and macOS gets a latency-critical activity (no
  App Nap; console "co-op: the game keeps playing in the background"). Port:
  the fps part is already engine-side; Windows/Linux need nothing else.
- `Sys_CoopMacName` (`C:258-264`): computer's display name (`NSHost
  localizedName`), "Mac" if none. Port: Windows `GetComputerNameExW(
  ComputerNameDnsHostname)`; Linux `gethostname()` (or the pretty hostname).

### 6.4 Co-op query client (`VSP_CoopQuery`, `C:475-572`)

Pure sockets; portable except `arc4random` and the Cocoa return type.

1. Trim. Empty → bad "Type the host’s address, such as 192.168.1.20."
2. More than one `:` → bad "IPv6 addresses aren’t supported yet. Use the host’s IPv4 address, such as 192.168.1.20."
3. `host:port`: port must be 1–5 digits, 1…65535, else bad `"“<p>” isn’t a port. Leave it out for the co-op port (29760), or give a number from 1 to 65535."`; empty host → bad "Type the host’s address before the port."
4. `getaddrinfo(host, AF_INET, SOCK_DGRAM)` fails → bad `"“<host>” couldn’t be found. Check the address."`. Shown address = `"<dotted ip>:<port>"`.
5. Socket fails → bad "A network socket couldn’t be opened."
6. Challenge = 8 lowercase hex digits (random). Query
   `\xff\xff\xff\xffgetcoopinfo <challenge>`, sent up to 3 times: at the start,
   when ⅔ and when ⅓ of the timeout remain; poll in slices of ≤ timeout/3.
7. Accept only packets from the exact ip:port, starting with
   `\xff\xff\xff\xffcoopInfoResponse\n`, whose infostring parses and whose
   `challenge` (if present) equals ours.
8. `coopProtocol != 7` or `version != EF_APP_VERSION` → mismatch: `"The host runs Elite Force for Mac <v> (co-op version <p>), and this Mac runs <mine> (co-op version 7). Both need the same version to play together."`
   Then the pak check (protocol 7): this computer's own paks (every `.pk3` in the homepath's baseEF and the game's BaseEF, by checksum, `FS_CoopCorePakIndex`) against the answer's `paks` with `CoopNet_ComparePaks`: pak0-pak2 differ → refused, shown with the sentences naming the packs (no Connect); only pak3 (Expansion Pack) differs → found with a warning and "Join Anyway".
9. Else found (with info). Timeout → none; if a send failed → error `"macOS couldn’t send to it (<strerror>). If you didn’t allow Elite Force to find devices on your local network, allow it in System Settings > Privacy & Security > Local Network."` (Port: firewall wording.)

Infostring parse (`C:439-461`): `\k\v\k\v…` → map; bytes as UTF-8, falling back
to Latin-1.

### 6.5 Host↔guest protocol (`sys_coop_net.c`)

UDP, port 29760, packets ≤ 1400 bytes (`COOP_MAX_PACKET`). Protocol version
`VSP_COOP_PROTOCOL = 7`; the app versions must also be **string-equal**
(`EF_APP_VERSION`, e.g. "1.3.0"), so a Windows/Linux build interoperates with a
Mac only at the same version string (and the same single-player paks). Byte
order little-endian; messages after connect are built with the engine's `msg_t`
in OOB mode (`MSG_InitOOB`: no Huffman; `MSG_WriteLong/Short/Byte/String/Data`
— `MSG_WriteString` turns bytes > 127 and `%` into `.`).

**Connect handshake** (OOB text: `\xff\xff\xff\xff<word>\n<infostring or sentence>`):

| Dir | Word | Payload |
|---|---|---|
| guest→host | `coopConnect` | `coopProtocol`, `version`, `paks` (`FS_CoopPaks()`: the game's own paks loaded, `"pak0.pk3:<checksum> pak1.pk3:<checksum> ..."`), `name` (clean, else "Player 2"), `sex` (optional `male`/`female`), `look` (optional: a player 2 character id from `coop_characters.h`, lower case letters, digits and `_`, at most 23; the host takes only one it knows with all files in its own pak0-pak2), `nonce` (16 hex), `password` (if any) |
| host→guest | `coopAccept` | `session` (decimal, 1…0x7ffffffe), `nonce` (echo), `paks` (the host's), `hostName`, `state`, `map`, `time`, `players` |
| host→guest | `coopReject` | sentence |

Host rules (`N:969-1071`): ≤ 5 connect requests per second from anyone; the
same guest (address + nonce) gets the same accept again; protocol/version
mismatch → `"The host runs Elite Force for Mac <v> (co-op version 7), and you run <v'> (co-op version <p'>). Both need the same version."`;
another address while a guest is active → `"The game already has a player 2 (<name>)."`;
pak0-pak2 differ (`CoopNet_ComparePaks`) → `"Your game files differ from the host's. You're missing pak2.pk3 (the host has it). Both need the game's original pak0.pk3, pak1.pk3 and pak2.pk3 (other packs don't matter)."` (each differing pack named); only pak3 differs → accepted, with a note on both consoles;
password set and wrong → "Wrong password." / missing → "This game needs a password.";
same address, new nonce → drop the old session silently and accept.
Guest: up to 5 tries 1 s apart (`COOP_CONNECT_TRIES`, `COOP_CONNECT_RETRY`);
connected UDP socket (so an ICMP port-unreachable shows as `ECONNREFUSED`),
receive buffer 8 MB.

**Session messages**: header `long session` (never −1, so never OOB) + `byte type`:

| Type | Dir | Body |
|---|---|---|
| 1 INPUT | g→h | `byte count` (1…4), per command: `long seq`, `short angles[3]` (0…65535), `long buttons`, `byte weapon`, `byte forwardmove`, `byte rightmove`, `byte upmove` (signed), `long viewTime`; then `long newestFrame` (ack), `long readyLevel` |
| 2 KEEPALIVE | g→h | `long newestFrame`, `long readyLevel` — sent when nothing was sent for 1 s or a new frame was decoded |
| 3 STATUS | h→g | `string` infostring: `hostName`, `state`, `map`, `time`, `players`, `paused` 0/1, `frame` — once a second and immediately when pause changes |
| 4 DISCONNECT | both | `string` reason |
| 5 FRAME | h→g | `long frame`, `short index`, `short count`, `long zlen`, `long rawlen`, then the fragment's compressed bytes (≤ 1200) to the end of the packet |

Timers: 10 s without a packet → drop ("timed out" / guest: "lost the connection
to the host (nothing heard for 10 seconds)"); guest input older than 500 ms
doesn't steer player 2; host queue 3 commands (overflow drops the oldest but
ORs its buttons into the next); guest re-sends its last 4 commands per packet.

**Replica frames** (`N:428-645`, `N:754-866`, `N:1289-1421`, `N:1636-1743`):
2050 slots (0…1023 entities, 1024 configstrings, 2048 globals, 2049 server
commands). Raw frame encoding:

```
long frame, long base (0 = from nothing), long levelId, long levelTime, long ackCmd,
map name NUL-terminated,
repeat { u16 slot (LE), byte kind:
           0 = now empty
           1 = whole: varint len, bytes
           2 = changed: varint newLen, then runs of (varint skip, varint len, bytes),
               skip counted from the end of the previous run; a (0,0) pair ends them;
               bytes past the base's length start as zeros }
u16 0xffff  (end)
```

Varints: 7 bits per byte, low first, high bit = more. A changed run ends after 8
equal bytes. The delta base is the newest frame the guest acked (32-frame ring
on both sides); without one a whole frame is sent at most once per 1000 ms until
acked. The encoded frame is **compressed** and split into ⌈zlen/1200⌉ fragments
(≤ 2048); host send buffer 4 MB. Guest keeps 4 assembly slots, decompresses,
requires decompressed length == `rawlen` (≥ 8, ≤ 64 MB), finds the base in its
ring (else drops), decodes, acks. On a frame for a level it hasn't loaded the
guest sets `sp_coop 2` and runs `map <name>` (name must be `[A-Za-z0-9_/-]`).

**Compression — exactly which algorithm**: `compression_encode_buffer(...,
COMPRESSION_ZLIB)` / `compression_decode_buffer(..., COMPRESSION_ZLIB)` (Apple
libcompression, `N:89,1384,1703`; linked as `compression`,
`engine/CMakeLists.txt:324-327`). Apple's `COMPRESSION_ZLIB` is **raw DEFLATE
(RFC 1951) with no zlib header and no Adler-32 trailer** (Apple's encoder uses
level 5). The portable equivalent:

```c
deflateInit2(&z, 5, Z_DEFLATED, -15 /* raw */, 8, Z_DEFAULT_STRATEGY);  // encode
inflateInit2(&z, -15);                                                  // decode
```

Any conforming raw-deflate stream interoperates (byte-identical output isn't
needed). **Caveat**: the engine's bundled `efcode/zlib/` has only the inflate
side (adler32, crc32, inffast, inflate, inftrees, zutil — no `deflate.c` /
`trees.c`), so a Windows/Linux co-op build must add zlib's deflate sources (or
link system zlib / a static zlib, or miniz). Note the GOG code uses
`inflateInit` (zlib **with** header) — a different stream format from co-op's.

Other Apple/POSIX-isms in `sys_coop_net.c`: `arc4random()` (session, nonce,
sim loss; glibc ≥ 2.36 has it, Windows doesn't — use `rand_s` /
`getrandom`); BSD sockets (`<sys/socket.h>`, `fcntl O_NONBLOCK`, `close`,
`errno`/`ECONNREFUSED`) — Windows needs Winsock (`ioctlsocket FIONBIO`,
`closesocket`, `WSAGetLastError`; on Windows an ICMP unreachable on a
connected UDP socket surfaces as `WSAECONNRESET`); `gettimeofday` (stats
only). Everything else (msg_t, Info_*, Cvar_*, Cmd_*) is engine-portable. The
`sp/sp_bridge.cpp` side (`EF_COOP` is also passed to `sp_game_bridge` only on
Apple, `engine/CMakeLists.txt:249`) must be enabled for Windows/Linux too.

Console messages users read (keep identical): `N:949,965,1069,1543,1593,1598,
1608,1618,1783,1795,1817,1932,2051-2053,2062,2136-2171`.

---

## 7. Other launcher features

### 7.1 `stvef://` links (`L:2014-2131`, co-op `C:764-786`)

Registered URL scheme `stvef` (`macos/Info.plist.in`: `CFBundleURLTypes`, name
"Star Trek Voyager: Elite Force server"). The handler is installed when the
launcher starts (after SDL's own) and stays for the life of the process.

Parsing (`LinkAddress`, `L:2027-2056`), strict because the result goes on a
command line:

1. Must start with `stvef:` (case-insensitive); strip it and any leading `/`.
2. `connect/<addr>[?…][#…][/…]` (efservers.com; github.com/stefansundin/stvef-protocol):
   addr = text up to the first `?`, `#` or `/`, percent-decoded.
3. Else legacy `stvef:<ip>;<port>;<name>;<map>;…`: needs ≥ 2 `;`-fields → `<f0>:<f1>`.
4. Valid iff 1…255 chars and matches `^([A-Za-z0-9.-]+|\[[0-9A-Fa-f:.]+\])(:[0-9]{1,5})?$`.

Co-op invites (EF_COOP, checked first): `stvef://coop/<host>[:<port>]` parsed
the same way (after `coop/`, cut at `?#/`, percent-decoded, same regex); a coop
link without a valid address yields `""`.

Behavior (`L:2061-2120`):

- **Co-op link**: activate the app. Launcher up and address valid → close any
  attached sheet, select Single Player, open the Co-op sheet with the address
  filled in and Check pressed (`VSP_CoopOpenJoin`). No address → info dialog
  "Join co-op": `"The co-op invite <link> has no address in it."`. Launcher not
  up (in game) → info dialog "Join co-op": `"To join the co-op game at <addr>, quit and open the invite again: it opens Co-op in the launcher with the address filled in."`
- **Server link**, launcher up: invalid → alert "This link can't be joined" /
  `"<link>\n\nIt doesn't hold a server address."`; valid → close any sheet,
  join (Holomatch, no password) and **Play immediately**.
- **Server link, in game**: invalid → console "Link <link> has no server
  address"; Holomatch → `connect <addr>` into the command buffer; single player
  → info dialog "Join a server": `"Single player can't join servers. To join <addr>, quit and open the link again, or pick Holomatch in the launcher."`
- Log `Link: <link> -> <addr | not a server address>`.

Port: Windows: register `HKCU\Software\Classes\stvef` (`URL Protocol` value,
`shell\open\command = "<exe>" --uri "%1"`); Linux: `.desktop` with
`MimeType=x-scheme-handler/stvef;` and `Exec=… --uri %u`. A link opened while
the app runs arrives as a **new process**: it must forward the URL to the
running instance (named pipe / local socket / D-Bus) and exit; if no instance
runs, it starts normally with the link pending (handled once the launcher is up,
like macOS's queued Apple event). `sys_main.c` already stops argv parsing at
`--uri` (`M:969-984`, ioq3's `PROTOCOL_HANDLER`, compiled out here and expecting
a different scheme): the launcher should consume `--uri stvef:…` itself.

### 7.2 About panel

Standard about panel (app name, icon, version `CFBundleShortVersionString`,
build `CFBundleVersion`, credits from `Contents/Resources/Credits.html`,
copyright `NSHumanReadableCopyright`). Credits text: `macos/Credits.html`
(Marino Hardin; VoyagerSP-Android by imjustadudegamer, lilium-voyager by Zack
Middleton, ioquake3, id Software, the Quake3e Vulkan renderer by Eugene C.,
Raven Software; cMod by Noah Metzger (Chomenor) thanks and permission note;
Paramount/Raven/Activision disclaimer). Copyright string: "Made by Marino
Hardin. Engine: GPLv2 (ioquake3 / lilium-voyager / Quake3e). Game code: Raven
Software Limited Use Software License. Game data not included." While the
launcher is modal, About is retargeted so it works and appears above the
launcher at modal-panel level (`L:1940-1956`). Port: an About dialog reachable
from the launcher (Windows: system-menu item or a small "About" link; Qt:
`QMessageBox::about` with the same HTML).

### 7.3 App menu during the launcher (`L:1920-2009`)

SDL's app-menu items target the responder chain, which a modal session blocks;
while the launcher (and the server window) are modal, About, Hide, Hide Others,
Show All and Quit are retargeted to working actions (Quit = launcher Quit). The
update items are inserted after About (§8.4). macOS-only mechanics; for parity
Windows/Linux need: About, Check for Updates…, Check for Updates Automatically
(checkable), Quit — e.g. a "⋯"/gear menu in the header, or the window's system
menu.

### 7.4 Logging

`VSP_Log`/`LLog` → `MacLog` (`D:115-134`): before `Com_Init` to stderr **and**
an in-memory early log, replayed into the console by `Sys_MacDataPostInit`;
after init via `Com_Printf`. Prefixes in use: `Launcher:`, `Game data:`,
`Updates:`, `Link:`, `Co-op:`, `GOG installer:`, `[dialog]`, `[test]`. Port:
sys_data.c's `DataLog`/`Sys_DataPrintEarlyLog` is the equivalent (8 KB fixed
buffer — the launcher logs more; grow it dynamically).

No crash reporter, help links or first-run tutorial in the launcher. First-run
behavior = the defaults above: mode Single Player, Best preset, 1920×1080
window (always listed), Standard brightness, name = first name, cMod on,
Loopback Host Fix on, update check on, "No game files yet" with Play disabled
and "Choose…".

### 7.5 Test-background mode, Dock/menu bar

`VOYAGERSP_TEST_BACKGROUND=1` (`O:101-209`): accessory app (no Dock icon, no
menu bar), windows kept one level below normal behind other apps, never key,
no activation, alerts go to stderr with defaults. Normal mode: regular Dock
app; SDL builds the menu bar; the File menu is added in game (§5.2).

---

## 8. Update checker (`L:1650-1918`)

- Repository `marinohardin/EliteForce-MacOS-Marino`; tag prefix **`macos-v`**
  (releases `macos-v1.1.0` … `macos-v1.3.0`; `v1.x` tags are the Android
  project's, `docs/PUBLISHING.md:102`). Port: per-platform prefixes (e.g.
  `windows-v`, `linux-v`) or a shared one; make it a parameter.
- Request: `GET https://api.github.com/repos/<repo>/releases?per_page=100`,
  headers `Accept: application/vnd.github+json`, `User-Agent:
  EliteForce-MacOS/<app version>`; no cookies/cache (ephemeral session);
  request timeout 15 s.
- Response handling: HTTP 200 and a JSON array required, else problem = the
  transport error text or `"GitHub answered <code>."`. For each element: object
  with string `tag_name` starting with the prefix, `draft` false, `prerelease`
  false; version = tag minus prefix; keep the greatest version (comparison
  below) and its `html_url` (string) as URL. Result: `(version, url)`; no release
  → `(nil, "https://github.com/<repo>/releases")`; failure → `(nil, problem)`.
  Callback on the UI thread. "Failure" is detected as "no version and the
  second value doesn't start with `https:`". Release URL fallback when
  `html_url` is missing: `https://github.com/<repo>/releases`.
- **Version comparison**: `NSString compare:options:NSNumericSearch`:
  digit runs compare numerically, other characters by character code; if one
  string is a prefix of the other the longer is greater ("1.3" < "1.3.0").
  `IsNewer(v)` = v non-empty, app version non-empty, v > app version. App
  version = `CFBundleShortVersionString` (= `EF_APP_VERSION`).
- **Automatic check** (`startUpdateCheck`, `L:1782-1804`), at launcher start:
  if disabled (`CheckForUpdates` = NO) do nothing. If `UpdateLatest` is stored,
  show it at once (badge if newer). If `UpdateLastCheck` is a date with
  `0 ≤ now − last < 86400 s`, stop (throttle: once a day; a future date
  re-checks). Else fetch; failure → log `Updates: couldn't check (<problem>)`,
  stay quiet, no timestamp (retry next start). Success → save (`UpdateLastCheck
  = now`; `UpdateLatest = {version,url}` or removed), log `Updates: latest
  release <v | (none)>, this is <mine>`, show/hide the badge.
- **Badge**: shown iff newer; Download… opens the stored URL. Nothing is
  downloaded or installed by the app.
- **Check for Updates…** (`L:1826-1870`): fetch now (ignores the throttle);
  on success save + update badge. Sheet alert:
  - failure: "Couldn't check for updates." / `"<problem> Check your internet connection and try again."` (OK)
  - newer: `"Elite Force for Mac <v> is available."` / `"You have version <mine>. Download the new version from its release page on GitHub, then replace the app in Applications. Your settings and saves are kept."` buttons "Download…" | "Later"; Download… opens the page.
  - else: "You're up to date." / `"Elite Force for Mac <mine> is the newest version."`
  (Port: reword "for Mac" / "replace the app in Applications" per platform.)
- **Check for Updates Automatically** (checkmark = enabled): toggles
  `CheckForUpdates`; on → `startUpdateCheck` (respecting the throttle); off →
  hide the badge. Log `Updates: automatic check on|off`.
- **Test hooks**: `VOYAGERSP_TEST_UPDATE=<version>|none` — answer immediately
  without network (`none` → no release; URL
  `https://github.com/<repo>/releases/tag/macos-v<version>`), bypasses the
  throttle, and nothing is saved. `VOYAGERSP_TEST_UPDATE_ALERT=<png>` — 0.5 s
  after showing, run Check for Updates…, log `[test] Check for Updates: "<msg>"
  "<info>"`, render the alert, quit.
- Unrelated legacy: `sys_main.c` still calls ioq3's `Sys_LaunchAutoupdater`
  (`M:875-876`, `sys_autoupdater.c`); the launcher's checker does not use it.

### 8.4 Menu placement

"Check for Updates…" and "Check for Updates Automatically" are inserted into
the application menu right after About, only while the launcher is up, and
removed when it closes (`L:1894-1916`); they work while modal.

---

## 9. Environment-variable test hooks

Keep these (or equivalents) so the existing regression scripts can drive the
Windows/Linux launchers. All are read with `getenv`; "set" = non-empty unless
noted.

Launcher (`L:35-79`, `L:2246-2405`):

| Variable | Effect |
|---|---|
| `VOYAGERSP_TEST_BACKGROUND=1` (not empty, not 0) | quiet background mode (§7.5) |
| `VOYAGERSP_TEST_LAUNCHER_SNAPSHOT=<png>` | render the window to PNG, then quit (unless `..._PLAY` or `VOYAGERSP_TEST_SERVER_START`) |
| `VOYAGERSP_TEST_LAUNCHER_APPEARANCE=dark\|light` | force appearance |
| `VOYAGERSP_TEST_LAUNCHER_MODE=sp\|hm\|host` | preselect mode (anything else → sp) |
| `VOYAGERSP_TEST_LAUNCHER_PRESET=performance\|balanced\|best` | apply a quality preset (unknown → best) |
| `VOYAGERSP_TEST_LAUNCHER_BRIGHTNESS=standard\|plain\|contrast\|bright\|fullbright` | select a brightness preset |
| `VOYAGERSP_TEST_LAUNCHER_PLAY=1` | press Play without showing the window |
| `VOYAGERSP_TEST_LAUNCHER_AUTOPLAY=<s>` | show it, press Play after s seconds (logs visibility) |
| `VOYAGERSP_TEST_ADD=<path>` | run Add Game Files… at start with that path instead of the panel |
| `VOYAGERSP_TEST_MASTERS=a,b,…` | set the master list |
| `VOYAGERSP_TEST_MASTERS_SNAPSHOT=<png>` | render Servers…, quit unless SNAPSHOT/PLAY |
| `VOYAGERSP_TEST_BROWSER_SNAPSHOT=<png>`, `_TAB=local\|combined\|<n>`, `_JOIN=<row>`, `_EXPANSION=anyway\|cancel`, `_SELECT=<row>`, `_PREVIEW=<png>`, `_PREVIEW_3D=1`, `_HOLD=<s>` | server browser (S); JOIN → plays Holomatch connecting (sets `..._LAUNCHER_PLAY`) |
| `VOYAGERSP_TEST_LAUNCHER_BROWSE_CANCEL=1` | open Browse…, press Cancel, log whether the launcher stayed, quit |
| `VOYAGERSP_TEST_SERVER_RUNNING=show\|another\|cancel` | answer Start Server's "already running" alert (logged) |
| `VOYAGERSP_TEST_SERVER_START`, `VOYAGERSP_TEST_SERVER_PORT=<n>`, `VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT=<png>`, `VOYAGERSP_TEST_SERVER_CONFIG_TAB` | server (S); SERVER_START = behave as `--host-server` |
| `VOYAGERSP_TEST_LAUNCHER_HUD=<png>` | render the HUD sheet and quit |
| `VOYAGERSP_TEST_LAUNCHER_ABOUT=<png>` | 1 s after showing: log app-menu items' enabled state, choose About, after 1.5 s render it, close, quit |
| `VOYAGERSP_TEST_UPDATE=<v>\|none`, `VOYAGERSP_TEST_UPDATE_ALERT=<png>` | §8 |
| `VOYAGERSP_TEST_LINK_HANDLER=<app>` | Browse… acts as if <app> handled stvef:// (S) |
| `VOYAGERSP_TEST_LINK=<stvef://…>` | 1 s after showing, deliver that link |
| `VOYAGERSP_TEST_NODIALOG=1` | `Sys_Dialog` prints to stderr and takes the default (a fatal error exits) |
| `VOYAGERSP_TEST_NAT=<a.b.c.d\|fail>` | pretend the router answered (or failed); also for co-op |

Game data / in-game (`D:45-59`):

| Variable | Effect |
|---|---|
| `VOYAGERSP_TEST_CHOOSE=<dir or exe>` | chooser uses that path instead of the panel (no retry loop) |
| `VOYAGERSP_TEST_INPLACE=1` | …with the in-place checkbox on |
| `VOYAGERSP_TEST_CONFIRM=1` | answer data/link alerts with the first button (still logged) |
| `VOYAGERSP_TEST_NOTRASH=1` | Delete Saves & Settings logs instead of trashing |
| `VOYAGERSP_TEST_NORELAUNCH=1` | log the relaunch instead of doing it |
| `VOYAGERSP_TEST_PANEL_TIMEOUT=<s>` | log the open panel's size and cancel after s |
| `VOYAGERSP_TEST_QUIT_ANSWER=quit\|cancel` | answer the Quit? sheet after 2 s |
| `VOYAGERSP_TEST_QUIT_PRESS=<s>:<front\|close\|cmdw\|cmdq\|key>,…` | scripted window actions after the first frame |

Co-op (`C:55-74`):

| Variable | Effect |
|---|---|
| `VOYAGERSP_TEST_COOP_SHEET=<png>` | 1 s after the launcher is up: open the sheet, render, then (after `_HOLD`) Cancel and quit |
| `VOYAGERSP_TEST_COOP_QUERY=<address>` | …first type it, press Check, wait (logs `[test] co-op check <a>: <status>`) |
| `VOYAGERSP_TEST_COOP_HOLD=<s>` | keep the sheet up after the snapshot (window numbers logged) |
| `VOYAGERSP_TEST_COOP_HOST=1` | open the sheet and press Host |
| `VOYAGERSP_TEST_COOP_INTERNET=1` | tick the router checkbox, wait ≤ 20 s for the answer, log it and the invite (clipboard untouched), snapshot, Cancel |
| `VOYAGERSP_TEST_COOP_PORT=<n>` | host on UDP n instead of 29760 |

When Start Server spawns the server instance, every `VOYAGERSP_TEST_*`
variable is removed from its environment (`L:1423-1427`).

The regression harness (`macos/regress.sh`) also uses `+set regress_*` cvars
(`qcommon/regress.c`), independent of the launcher.

---

## 10. Portable core candidates

Proposed shared library (`sys/launcher_core.{c,h}`, plain C99, no SDL/UI, usable
before `Com_Init` — no `Z_Malloc`, no `Com_Printf`; log through a callback).
Paths are UTF-8; Windows file calls go through wide-char helpers (as `SD`/`G` do).

### 10.1 Settings store

Replace NSUserDefaults with `<homepath>/launcher.cfg` (already where sys_data.c
reads `gamefolder`). One `key "value"` per line, last wins; unknown keys
preserved on rewrite.

```c
typedef struct lc_store_s lc_store_t;
lc_store_t *LC_StoreLoad( const char *homepath );            // reads launcher.cfg
const char *LC_StoreGet( lc_store_t *s, const char *key );   // NULL if absent
int         LC_StoreGetInt( lc_store_t *s, const char *key, int def );
void        LC_StoreSet( lc_store_t *s, const char *key, const char *value );  // NULL removes
int         LC_StoreSave( lc_store_t *s );                    // atomic: write .tmp, rename
void        LC_StoreFree( lc_store_t *s );
```

Keys: `gamefolder` (must stay compatible with `SD:486-517`), `launchermode`,
`checkforupdates`, `updatelastcheck` (unix time), `updatelatestversion`,
`updatelatesturl`, `hudwidescreen`, `coopkeepplayinginbackground`,
`coopplayername`, `coopguestmunro`, `loopbackhostfix`, plus the server spec's
keys (master list, server settings, browser favorites).

### 10.2 Config reading and graphics model

```c
typedef struct { int count; char (*names)[64]; char (*values)[256]; } lc_cfg_t;
int         LC_ReadConfig( const char *path, lc_cfg_t *out );          // §2.5 rules
const char *LC_CfgGet( const lc_cfg_t *c, const char *lowerName );
int         LC_CfgInt( const lc_cfg_t *c, const char *name, int def );
float       LC_CfgFloat( const lc_cfg_t *c, const char *name, float def );
void        LC_CfgFree( lc_cfg_t *c );
void        LC_ConfigPath( const char *homeBase, int holomatch, char *out, size_t size );  // efspconfig / hmconfig

typedef enum { LC_PRESET_PERFORMANCE, LC_PRESET_BALANCED, LC_PRESET_BEST, LC_PRESET_CUSTOM } lc_preset_t;
typedef struct { int fullscreen, width, height, highdpi, msaa, picmip, vsync, brightness; } lc_gfx_t;
#define LC_BRIGHTNESS_CUSTOM (-1)

void        LC_LoadGraphics( const char *homeBase, int holomatch, lc_gfx_t *g, int *fromConfig );   // §2.5
lc_preset_t LC_MatchPreset( const lc_gfx_t *g );
void        LC_ApplyPreset( lc_gfx_t *g, lc_preset_t p );
int         LC_MatchBrightness( const lc_cfg_t *cfg );                      // §2.6
int         LC_NumBrightnessPresets( void );
const char *LC_BrightnessTitle( int b );
const char *LC_BrightnessHelp( int b );      // b = -1: the Custom hint
const char *LC_BrightnessTestId( int b );
// window size list for a usable screen area (h already minus the title bar)
int         LC_WindowSizes( int maxW, int maxH, int curW, int curH, int (*out)[2], int max );
// cMod values from hmconfig + store (§2.8)
void        LC_LoadCmod( const lc_cfg_t *hm, lc_store_t *s, int *cmodOn, int *hudAspect,
                         char *hudCenter, size_t hcSize, int *predictWeapons );
void        LC_PlayerNameFromConfig( const lc_cfg_t *hm, const char *fullUserName, char *out, size_t size );
```

### 10.3 Names

```c
void LC_CleanName( const char *utf8In, char *out, size_t size );    // VSP_CleanName (§2.8), "UnnamedPlayer" fallback
void LC_FirstName( const char *fullName, char *out, size_t size );  // first word, else "Crewman"
void LC_CoopSafeName( const char *utf8In, char *out, size_t size ); // §6.2 (built-in diacritic fold)
```

### 10.4 Command line builder

```c
typedef struct {
    int         mode;                 // 0 SP, 1 HM (Host never reaches here)
    lc_gfx_t    gfx;
    const char *name;                 // raw; cleaned inside
    int         cmodAvailable, cmodOn, hudAspect, predictWeapons;
    const char *hudCenter;
    const char *basepath;             // NULL: none
    int         mastersCfgWritten;    // +exec vsp_masters.cfg
    const char *loopbackIP;           // NULL: none
    // co-op (EF_COOP)
    int         coopHost, coopBackground;
    const char *coopConnect, *coopSex, *coopName;
    // Browse…/link
    const char *joinAddress, *joinPassword;
} lc_launch_t;

// Appends exactly §3.2 to commandLine (size = total buffer, MAX_STRING_CHARS);
// writes vsp_brightness.cfg in homeBase when needed. Returns 0 on success.
int  LC_AppendLaunchArgs( const lc_launch_t *o, const char *homeBase, char *commandLine, int size );
// §3.3 as an argv array (for --host-server's Play on This Mac)
int  LC_ClientArgs( const lc_launch_t *o, const char *homeBase, char **argv, int maxArgs );
// §2.6
int  LC_BrightnessArgs( int b, size_t room, const char *homeBase, char *out, size_t outSize );
// log-safe copy (password masked as ••••)
void LC_MaskForLog( const char *args, const char *password, char *out, size_t size );
```

### 10.5 Game data

```c
typedef struct {
    int  found;                 // 0: nothing
    char basepath[1024];        // "" = default (homepath copy)
    char base[1024];            // the BaseEF folder
    char how[64];               // "remembered game folder", "homepath", "next to the game", "GOG install", …
} lc_data_t;

int  LC_DataOverridden( int argc, char **argv );                 // = Sys_DataOverridden
int  LC_FindGameData( int argc, char **argv, lc_data_t *out );   // Sys_DataResolve without exit/UI (§4.6)
int  LC_MissingPatchPaks( const char *homeBase, const char *base, char names[2][16] );  // count
int  LC_HasExpansion( const char *homeBase, const char *base );
int  LC_ZipHasEntry( const char *pk3, const char *name );
// chooser validation (§4.2 steps 2-5): 0 ok (base filled), else a problem sentence
int  LC_ValidateChoice( const char *chosen, int inPlace, char *base, size_t baseSize,
                        char *problem, size_t problemSize );
int  LC_IsInstaller( const char *path );                  // not a dir, ends ".exe"
int  LC_RememberGameFolder( const char *homepath, const char *folderHoldingBase ); // gamefolder line; NULL clears
// Linux in-place: the .gamedata symlink logic (SD UseFound) exposed for the launcher
const char *LC_UseFoundBasepath( const char *folder, const char *base );

// Copy plans and the worker (the UI polls progress / sets cancel)
typedef struct lc_copyjob_s lc_copyjob_t;   // items: plain file, or a GOG file (parts)
lc_copyjob_t *LC_PlanCopyGameFiles( const char *srcBase, const char *homeBase, int *pk3s, int *saves, int *kept );
lc_copyjob_t *LC_PlanAddFiles( const char **picked, int n, const char *homeBase, const char *externalBase, int *present );
lc_copyjob_t *LC_PlanUnpackInstaller( const char *exe, int onlyNew, const char *homeBase,
                                      const char *externalBase, int *present, char *err, size_t errSize );
unsigned long long LC_CopyJobTotal( const lc_copyjob_t *j );
int  LC_CopyJobIsUnpack( const lc_copyjob_t *j );
int  LC_CopyJobRun( lc_copyjob_t *j, volatile unsigned long long *progress, volatile signed char *cancel,
                    char *err, size_t errSize );   // on a worker thread; rolls back created files on failure
void LC_CopyJobFree( lc_copyjob_t *j );
unsigned long long LC_FreeSpace( const char *dir );        // 0 = unknown
void LC_FormatBytes( unsigned long long n, char *out, size_t size );  // "600 MB" (decimal)
```

`sys_osx_gog.c` is already portable and can be used as is (rename to
`sys_gog.c`; enable `EF_GOG` on Windows/Linux and link liblzma).

### 10.6 Relaunch, delete, homepath

```c
void LC_SetFromLauncher( void );
void LC_QuitToLauncher( void );            // the Sys_PlatformQuitToLauncher decision (§5.1); needs sys_quitSignal
void LC_RelaunchAtExit( int mode );        // -1 launcher, 0 SP, 1 HM; platform spawn via a callback
int  LC_SafeHomeRoot( const char *homepath, const char *expectedName, char *out, size_t size );   // §5.4
int  LC_DeleteSummary( const char *homepath, int *saves, int *files, unsigned long long *bytes,
                       int *pk3s, unsigned long long *pk3Bytes );
```

(The actual spawn and trash calls are platform functions; the decision logic is
shared.)

### 10.7 Updates

```c
// tagPrefix e.g. "macos-v"; 1 + version/url, 0 none (url = releases page), -1 parse failure
int  LC_ParseReleases( const char *json, size_t len, const char *tagPrefix, const char *repo,
                       char *version, size_t vSize, char *url, size_t uSize );
int  LC_VersionCompare( const char *a, const char *b );    // NSNumericSearch semantics (§8)
int  LC_UpdateDue( long long lastCheck, long long now );   // !(0 <= now-last < 86400)
void LC_ReleasesURL( const char *repo, char *out, size_t size );  // ...releases?per_page=100
```

HTTP: libcurl (already linked on macOS for map downloads) on Linux; WinHTTP or
libcurl on Windows. A tiny JSON scanner suffices (array of objects; keys
`tag_name`, `draft`, `prerelease`, `html_url`).

### 10.8 Links

```c
int LC_LinkAddress( const char *link, char *out, size_t size );      // §7.1; 0: not a server address
int LC_CoopLinkAddress( const char *link, char *out, size_t size );  // -1 not a coop link, 0 coop w/o address, 1 ok
```

(The regex is simple enough to hand-code: `[A-Za-z0-9.-]+` or `[` + hex/`:`/`.`
+ `]`, optional `:` + 1–5 digits.)

### 10.9 Co-op

Move to portable C (e.g. `sys/sys_coop_host.c`), with a small socket shim:

```c
int          Sys_CoopBackgroundPlay( void );
void         Sys_CoopFrame( void );
void         Sys_CoopMacName( char *out, int size );       // per-OS body (rename e.g. Sys_CoopHostName)
// launcher side
typedef struct { char status[12]; char address[64]; char error[512]; char info[MAX_INFO_STRING]; } lc_coopquery_t;
void LC_CoopQuery( const char *address, double timeout, lc_coopquery_t *out );   // §6.4 (blocking; run on a thread)
int  LC_CoopLANAddresses( char addrs[][16], char ifnames[][64], int max );     // primary first
void LC_CoopDescribe( const char *info, char *out, size_t size );               // §6.1 description lines
void LC_CoopInviteLink( const char *publicAddr, char *out, size_t size );
void LC_CoopAppendArgs( char *commandLine, int size, int holomatch );           // §6.2 (state set by the sheet)
void LC_CoopChooseHost( int background, const char *name, const char *password );
void LC_CoopChooseJoin( const char *address, const char *sex, const char *name, const char *password );
```

`sys_coop_net.c` changes: compression → zlib raw deflate (§6.5) behind
`CoopCompress()/CoopDecompress()`; `arc4random` → a portable random helper;
Winsock shim; add zlib's deflate sources to the build; enable `EF_COOP` for
`sp_game_bridge` on all desktop platforms. Its protocol and console behavior
stay identical so Mac and Windows/Linux copies can play together at the same
`EF_APP_VERSION`.

NAT port mapping (co-op checkbox, Loopback Host Fix public-address lookup, the
server's "open port") → one shared module on libnatpmp/miniupnpc with callbacks
matching §6.1's outcomes (unsupported, disabled, double NAT, error n, private
external address, 15 s timeout, success ip:port).

---

## 11. Platform UI only

Everything below is presentation or OS integration and must be rebuilt natively
(Win32 / Qt 6), following the texts and behavior specified above:

- Main window, header, update badge, mode cards (custom-drawn selectable cards
  with keyboard/accessibility), grouped sections, footer buttons and
  default/cancel keys (§2).
- Popups, segmented controls, checkboxes, sliders, tooltips; live enablement
  rules (§2.3–2.10).
- HUD… sheet, Co-op sheet (modal dialogs on Windows/Linux), Start Server alert,
  update alerts, all `Alert()`/`ErrorAlert()` dialogs, `Sys_Dialog`.
- Open dialogs (chooser with the in-place option; Add Game Files multi-select)
  and the copy-progress dialog with Cancel (worker in core).
- Reveal a folder in Explorer / the file manager (`ShellExecuteW` /
  `xdg-open`, `QDesktopServices::openUrl`); open a URL in the browser.
- Spawning the `--host-server` instance; activating a running server instance
  (Windows `AllowSetForegroundWindow`/`SetForegroundWindow`; Linux best
  effort); relaunch at exit; moving the homepath to the Recycle Bin / Trash.
- Clipboard (Copy Invite).
- URL scheme registration and single-instance forwarding (§7.1).
- App/system menu items: About, Check for Updates…, Check for Updates
  Automatically, Quit; the in-game File menu's equivalent (§5.2).
- About dialog with credits (§7.2).
- Dark mode (follow the OS: Windows 10+ `AppsUseLightTheme` /
  `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)`, Qt palette), accent
  color, SF Symbols → bundled icons (person, person.3, server.rack, checkmark,
  warning triangle, puzzle piece filled/outline, terminal, arrow.down.circle,
  lock, x-mark, question mark, exclamation circle).
- Screen size query for the window-size list (work area of the primary/current
  monitor, in logical pixels).
- Local-network privacy hints (macOS) → firewall hints (Windows Defender
  Firewall prompt on first bind; Linux usually none).
- Test-hook plumbing for snapshots (render a widget/window to PNG: Qt
  `QWidget::grab`, Win32 `PrintWindow`).
- Close-window "Quit?" confirmation in game (§5.3).
- macOS-only, no counterpart needed: bundled Vulkan env, press-and-hold default,
  App Translocation check, `ChooserPanelSizeReset`, modal app-menu retargeting,
  accessory-app background mode details, dispatch-source signal handling,
  Lilium Voyager homepath migration.
