# Launcher functional spec: servers (browser, masters, hosting)

Status: reference spec for the Windows (Win32) and Linux (Qt 6) launchers, written from the
macOS launcher at commit `34193c9`. The goal is 100% feature parity on a shared portable C
core. Implementers should not need to read the Objective-C; every string, number, rule and
timing that the macOS build has is written out here.

This document covers the **server-related** parts only:

- the Host a Server settings model and the **Server Settings** sheet ("Config…"),
- the **map catalog** (maps read from pk3 files without the engine),
- the **master server list** and its **Master Servers** sheet ("Servers…"),
- the **Holomatch Servers** browser sheet ("Browse…"), favourites, filters, players, Map Info,
- the **server window** (the dedicated server host: child process, console, status, controls,
  router port mapping, internet-listing check, Play on This Mac),
- **Loopback Host Fix** and the public-address lookup,
- the generated cfg files and persisted settings.

The main launcher window (modes, graphics, data, co-op, HUD, updates, links) is specified
elsewhere; only its entry points into this code are described here (section 1).

Contents: 0 Conventions · 1 Entry points · 2 Settings model · 3 Map catalog · 4 Server
Settings sheet · 5 Generated cfg files · 6 Master servers and the Master Servers sheet ·
7 Network protocols · 8 Server browser · 9 Hosting: the server window · 10 Loopback Host Fix ·
11 Persistence · 12 Threading and timing · 13 Icons · 14 Test hooks · 15 Portable core
candidates / Platform UI only · 16 Quirks to keep or fix.

## 0. Conventions

- **Source references.** `S:1234` is line 1234 of
  `EFAndroid-SP/app/jni/efcode/sys/sys_osx_server.m`; `L:1234` is
  `sys_osx_launcher.m`; `H:123` is `sys_osx_launcher.h`; all in the same directory.
  Engine references give their own path (e.g. `server/sv_init.c:640`).
- **Strings** in `"double quotes"` are exact UI text, including Unicode ellipses (`…`),
  middle dots (`·`), en dashes (`–`), curly quotes (`“ ”`) and arrows (`→`). Keep them.
  Text that the macOS build shows with the word "Mac" ("Play on This Mac", "this Mac",
  "(this Mac)") should become "PC" / "this PC" (or "this computer") on Windows/Linux; this
  is the only intended text difference. Everything else stays the same.
- **Colors.** macOS semantic colors are named as-is: *secondary* (dimmed label), *tertiary*
  (more dimmed), *quaternary*, *orange*, *red*, *green*, *blue*, *purple*, *yellow*, *teal*,
  *pink*, *accent* (the system accent colour). *Amber* is a custom colour: in light mode sRGB
  (0.70, 0.47, 0.00); in dark mode the system yellow (`S:4567-4575`).
- **Icons** are SF Symbols names (e.g. `checkmark.circle.fill`). Use an equivalent icon
  (Segoe Fluent Icons / Qt theme icons / bundled SVG). Section 13 lists every symbol used.
- **HomeRoot** is the per-user data folder. macOS: `~/Library/Application Support/EliteForce-MacOS`
  (`$HOME` honoured first). **HomeBase** is `HomeRoot/baseEF` (`sys_osx_data.m:143-154`).
  Windows/Linux use their engine homepath (`EF_HOMEPATH_NAME`, "EliteForce-Marino" on
  non-macOS per `sys/sys_platform.h:25-31`) with `baseEF` under it.
- **The engine executable** is the same binary as the launcher on macOS. On Windows/Linux
  "the app's own executable" means the game executable the launcher would start (on Linux
  inside an AppImage, the AppImage itself or the AppRun target; see the main launcher spec).
- **Settings store.** macOS uses `NSUserDefaults` (a per-app plist). Windows/Linux should use
  one store with the same key names (e.g. `QSettings` on Linux; registry `HKCU\Software\<app>`
  or an INI file under HomeRoot on Windows). Section 11 lists every key, its type and its
  "absent" meaning, which matters (several keys are *removed* to mean "the default").
- "**Sheet**" = a window-modal dialog attached to the launcher window. On Win32/Qt use a
  modal dialog owned by the launcher window.
- "**Log**" = the launcher's own log (`VSP_Log`/`LLog`, which on macOS goes to the early-log
  buffer and stderr; see the main spec). Log lines quoted here are exact, for test parity.

---

## 1. Entry points from the main launcher

The launcher (`sys_osx_launcher.m`) uses only the functions declared in
`sys_osx_launcher.h:47-160`. Each must exist in the portable implementation.

| Launcher action | Calls | Where |
|---|---|---|
| Launcher start: load server settings | `[VSPServerSettings load]` | `L:693` |
| Launcher start, test hook `VOYAGERSP_TEST_MASTERS=a,b,…` | `VSP_SetMasterServers(list)` (persisted) | `L:694-695` |
| Launcher start, `VOYAGERSP_TEST_SERVER_PORT` | overrides `server.port` in memory | `L:696-697` |
| Launcher start (after building the window) | `VSP_StartPublicAddressLookup()` | `L:699` |
| Host card "Config…" button | selects Host mode, then `VSP_ShowServerConfig(window, server, dataFolder, done)`; `done` refreshes the Server summary | `L:1202-1208` |
| Host-mode Server summary group | `VSP_MapCatalog`, `VSP_FindMap`, `modifierNames`, `gametypeName`, `skillName`, `VSP_LANAddress` | `L:1151-1200` |
| Holomatch card "Browse…" button | selects Holomatch mode, then `VSP_ShowServerBrowser(window, dataFolder, done(address,password))`; on an address: remember it, then Play | `L:1226-1242` |
| "Internet games:" row "Servers…" button | `VSP_ShowMasterServers(window, done)`; `done` refreshes the summary | `L:1244-1249` |
| "Internet games:" summary label | `VSP_MasterServers()` | `L:1210-1218` |
| "Loopback Host Fix" checkbox | `VSP_LoopbackFix()` (initial), `VSP_SetLoopbackFix(on)` (on toggle) | `L:915-918`, `L:1220-1223` |
| cMod "Game modules:" enabling | `VSP_CmodAvailable()` | `L:1136-1138` |
| Play (Holomatch) / server instance's client args | `VSP_WriteMastersConfig()` then `+exec vsp_masters.cfg`; `VSP_LoopbackArgs()` / `VSP_PublicAddress()` | `L:1583-1599`, `L:1626-1630` |
| "Start Server" (the Play button in Host mode) | `VSP_RunningServers()`, then launch a new app instance with `--host-server` | `L:1376-1450` |
| Instance started with `--host-server` | `VSP_RunServerWindow(settings, basepath, dataFolder, clientArgs)`, then `exit(0)` | `L:2250-2252`, `L:2316-2325` |
| Test snapshots | `VSP_SnapshotServerConfig`, `VSP_SnapshotMasterServers`, `VSP_SnapshotServerBrowser` | `L:2264-2305` |

Join via Browse… (`L:1226-1242`, `L:1626-1640`): the launcher stores `joinAddress` and
`joinPassword`, selects Holomatch and presses Play. The Holomatch command line then ends with
`+set password "<pw>"` (only when one was given) and `+connect <address>`. The launcher's log
masks the password as `••••`.

### 1.1 Host-mode summary in the launcher window (`L:865-890`, `L:1151-1200`)

With the Host a Server mode selected, the launcher shows a "Server" grid in place of the
graphics group. Rows (label column 112 pt, right-aligned labels, 13 pt):

| Label | Value |
|---|---|
| "Server name:" | `hostname` |
| "Game:" | `gametypeName`, or `"<gametypeName> · <modifier names joined by ", ">"` |
| "Maps:" | see below |
| "Players:" | `"Up to <maxPlayers>"` + (bots > 0: `" · bots fill to <bots>< per team, if gametype ≥ 3> (<skillName>)"`; else `" · no bots"`) |
| "Network:" | `"<net> · port <port><warn><pw>"`; net = `"Internet, port opened on the router"` (internet + openPort) / `"Internet"` / `"Local network only"`; warn = `" (not listed in the Join menu)"` when the port is outside 27960–27963; pw = `" · password"` when a password is set. Orange when the port is outside 27960–27963. |
| "Address:" | `"<LAN address, or 127.0.0.1>:<port>"`, monospaced digits, 13 pt medium |
| (none) | hint, 11 pt secondary, wraps at 400: "Friends connect to this address, or find the server in Holomatch's Join menu. It runs in its own window, where Play on This Mac joins it with your Holomatch graphics settings." |

Maps value: rotation empty → `"None chosen yet"`; 1 entry → its display name, or
`"1 missing map"` if it is not in the catalog; N entries →
`"<N> in rotation: <display names of the found maps joined by ", ">"`, plus
`" (<missing> missing)"` when some are missing. Orange when any are missing or the rotation is
empty. Tooltip: rotation names joined by `" → "`.

### 1.2 "Internet games:" row (Holomatch area of the launcher, `L:900-935`, `L:1210-1223`)

- Summary label: no masters → `"No master servers"` (orange); 1 → `"1 master server · <m1>"`;
  N → `"<N> master servers · Internet 1: <m1>"`. Tooltip: all masters, newline-separated.
- Button "Servers…" (small), tooltip "The master servers that list internet games, and that a
  server you host announces itself to". Opens the Master Servers sheet (section 6).
- Checkbox "Loopback Host Fix", tooltip "Most routers don't route this Mac back to its own
  public address, so a server hosted here wouldn't answer in your own internet lists. This lists
  it as this Mac (127.0.0.1) instead." Below it, 11 pt secondary: "Shows a server you host on
  this Mac in your own internet lists." Toggling writes the setting immediately (section 10).

### 1.3 Start Server (`L:1376-1450`)

Pressing the main button in Host mode (its title is "Start Server" in that mode, `L:1141`):

1. Persist: `LauncherMode = 2` (host), `ServerSettings` saved, store flushed.
2. `running = VSP_RunningServers()` (section 9.13). If non-empty, show an app-modal alert:
   - Message: 1 running → "A server is already running on this Mac."; N → "<N> servers are already running on this Mac."
   - Informative: `“<name of the LAST (newest) entry, or "Server" if empty>” is running on port <port>. Another server would start on the next free port, with the settings in Config…; players would see both in the server list.`
   - Buttons, in order: "Show Running Server" (default), "Start Another", "Cancel" (Escape).
   - Show Running Server: bring that pid's application to the front (all its windows), stop.
     (Windows: `AllowSetForegroundWindow` + find the instance's top-level window by pid;
     Linux/Qt: no portable activation — at least raise via the instance's single-instance IPC,
     or a no-op with a log line.)
   - Cancel: stop. Start Another: continue.
3. Start a **new instance** of the app with the single argument `--host-server`, the current
   environment minus every variable whose name starts with `VOYAGERSP_TEST_`, activated.
   (macOS: `NSWorkspace openApplicationAtURL` with `createsNewApplicationInstance`.) On
   Windows/Linux: spawn the launcher executable, detached, with `--host-server`.
   Log `Launcher: starting the server in its own window`, then on success
   `Launcher: server window started, pid <pid>`.
4. If the launch fails: alert (sheet) "The server couldn't be started." with the OS error text.

The launcher stays open; the server runs in the other instance, which shows only the server
window (section 9) and exits when that window closes.

### 1.4 The `--host-server` instance (`L:2246-2325`)

When `--host-server` is among argv (or `VOYAGERSP_TEST_SERVER_START` is set), the app builds
the launcher object (without showing it) to load the same settings, game-data location,
graphics and name, then calls `VSP_RunServerWindow(server, basepath, dataFolder, clientArgs)`
and exits with status 0. If no game data is found it only logs `Server: no game files found`
and still opens the window (the child then exits 1 and the window explains it).

`clientArgs` (`L:1583-1599`), used by Play on This Mac, is the Holomatch client command line
the launcher would use, as an argv array, computed once when the instance starts:

```
+set com_holomatch 1 +set r_fullscreen <0|1> +set r_mode -1 +set r_customwidth <w>
+set r_customheight <h> +set r_highdpi <0|1> +set r_picmip <n> +set r_swapInterval <0|1>
+set r_fbo 1 +set r_ext_multisample <n> +set name <cleaned player name>
<brightness args (≤600 chars)> <cMod args> [+set fs_basepath <basepath>]
[+exec vsp_masters.cfg      (only if writing it succeeded)]
[+set cl_loopbackHostIP <ip> (VSP_LoopbackArgs; may wait up to 1.5 s)]
```

(Graphics, brightness and cMod pieces are specified in the main launcher spec.)

---

## 2. Server settings model (`VSPServerSettings`, `S:140-321`, `H:51-79`)

### 2.1 Fields, defaults and stored types

Stored as one dictionary under the settings key **`ServerSettings`** with exactly these 26
keys (`S:209-215`). Types: S = string, I = integer, B = boolean, A = array of strings.

| Key | Type | Default (`S:175-200`) | Meaning / engine cvar |
|---|---|---|---|
| `hostname` | S | `"<first word of the user's full name>'s Server"`, or `"Elite Force Server"` | `sv_hostname` |
| `password` | S | `""` | `g_password` |
| `gametype` | I | `0` | `g_gametype`: 0 Free For All, 1 Tournament, 3 Team Holomatch, 4 Capture the Flag |
| `assimilation` | B | NO | `g_pModAssimilation` |
| `disintegration` | B | NO | `g_pModDisintegration` |
| `actionHero` | B | NO | `g_pModActionHero` |
| `specialties` | B | NO | `g_pModSpecialties` |
| `elimination` | B | NO | `g_pModElimination` |
| `fraglimit` | I | 20 | `fraglimit` (0 = none) |
| `capturelimit` | I | 8 | `capturelimit` (0 = none) |
| `timelimit` | I | 15 | `timelimit`, minutes (0 = none) |
| `maxPlayers` | I | 12 | `sv_maxclients` (bots included) |
| `bots` | I | 0 | `bot_minplayers` (fill to N players; per team in team games) |
| `botSkill` | I | 3 | `g_spSkill` 1–5 |
| `friendlyFire` | B | NO | `g_friendlyFire` |
| `internet` | B | NO | `dedicated 2` (internet, heartbeats to the masters) vs `dedicated 1` (LAN) |
| `openPort` | B | YES | ask the router to forward the port (internet only) |
| `allowDownload` | B | YES | `sv_allowDownload` |
| `pure` | B | YES | `sv_pure` |
| `port` | I | 27960 | `net_port` |
| `tickRate` | I | 20 | 20 = Standard (sv_fps not written), 40 = High (`set sv_fps 40`) |
| `cmodGame` | B | YES | `sv_cmodGame` |
| `unlagged` | I | 1 | `g_unlagged` 0/1/2 |
| `smoothClients` | B | YES | `g_smoothClients` |
| `unlaggedPredict` | B | YES | `g_unlaggedPredict` |
| `rotation` | A | `hm_voy1, hm_borg1, hm_kln1, hm_dn1, hm_for1, hm_cam` | map names, in play order |

Default hostname (`S:160-165`): take the OS user's full name (macOS `NSFullUserName()`;
Windows `GetUserNameExW(NameDisplay)` falling back to the login name; Linux the GECOS field of
`getpwuid(getuid())` up to the first comma, falling back to the login name), split on spaces,
take the first word, clean it with `CleanCvarText(word, 24)`; if non-empty the default is
`<word>'s Server` (ASCII apostrophe), else `Elite Force Server`.

### 2.2 Cleaning (`S:144-158`, `S:259-260`)

`CleanCvarText(text, maxLen)`: walk the characters, **drop** any with code < 32 or > 126 and
any of `"` `;` `\` `%`; stop once the output has `maxLen` characters; then trim leading and
trailing whitespace. Applied on **every assignment** (in the property setters):

- `hostname` → `CleanCvarText(v, 48)`
- `password` → `CleanCvarText(v, 32)`

Because it runs in the setter, the Server Name and Password fields update the (cleaned) model
on each keystroke; the text fields keep what was typed until the sheet is reopened.

### 2.3 Loading and validation (`S:224-251`)

1. Start from the defaults.
2. Read the stored dictionary. For each of the 26 keys, take the stored value only if it has
   the right kind (string for S, array for A, number for I/B). Unknown keys are ignored.
   (Array element types are not checked; treat non-strings as absent.)
3. Clamp/repair:
   - `gametype` not in {0, 1, 3, 4} → 0.
   - `botSkill` clamped to 1–5; `maxPlayers` to 2–32; `bots` to 0–31.
   - `port` < 1024 or > 65535 → 27960.
   - `tickRate` ≠ 40 → 20.
   - `unlagged` clamped to 0–2.
   - empty `hostname` (after cleaning) → the default hostname.
4. `applyModifierRules(favoring: none)` (2.4).

`fraglimit`, `capturelimit`, `timelimit` and `rotation` are not validated on load.
Saving (`S:253-257`) writes all 26 keys and flushes the store.

### 2.4 Modifier rules (`S:262-290`)

The retail Holomatch ui.qvm's Create Server rules. Fixed order of modifiers (also the display
order): `assimilation, specialties, disintegration, elimination, actionHero` (`S:167-171`).

```
excludes:
  assimilation   -> disintegration, elimination, actionHero
  specialties    -> disintegration, actionHero
  disintegration -> assimilation, actionHero, specialties
  elimination    -> assimilation, actionHero
  actionHero     -> assimilation, specialties, elimination, disintegration

applyModifierRules(favoring key):
  if gametype != 3 (Team)  : assimilation = OFF
  if gametype >= 4 (CTF)   : elimination  = OFF
  if gametype >= 3 (Team+) : actionHero   = OFF
  order = the five above; if key is one of them, move it to the front
  for m in order: if m is ON: set every modifier in excludes[m] OFF
```

The modifier the user just turned on wins; without a favourite, earlier ones in the fixed
order win.

### 2.5 Display names

- `gametypeName` (`S:304-313`): 1 "Tournament", 3 "Team Holomatch", 4 "Capture the Flag",
  anything else "Free For All".
- `skillName` (`S:315-319`): 1 "Cadet", 2 "Ensign", 3 "Lieutenant", 4 "Commander", 5 "Captain"
  (input clamped to 1–5).
- `modifierNames` (`S:292-302`): the modifiers that are ON, in the fixed order, named
  "Assimilation", "Specialties", "Disintegration", "Elimination", "Action Hero".

### 2.6 cMod availability (`S:122-132`)

`VSP_CmodAvailable()` is true when the bundled cMod folder (macOS
`<app>/Contents/Resources/cmod`) contains at least one file whose extension is `pk3`
(case-insensitive). Windows/Linux: the equivalent bundled `cmod` directory (next to the
executable / inside the AppImage). It disables the cMod page's controls (4.5) but does not
change the stored `cmodGame`, which is still written to the cfg.

---

## 3. Map catalog (`S:323-760`)

`VSP_MapCatalog(dataFolder)` returns the Holomatch maps found in the game files, read
directly from the pk3 zip files, without the engine.

### 3.1 Directories (`S:618-628`)

In order: `dataFolder` (the game's `baseEF` folder chosen in the launcher; may be absent), then
HomeBase (only if different after path standardisation). Later directories **win** (as in the
engine, the homepath overrides).

### 3.2 Scan (`S:649-752`)

For each directory, in order:

1. List its entries, sorted case-insensitively. For each file not starting with `.` whose
   extension is `pk3` (case-insensitive):
   - `retail` = base name (lowercased, no extension) is exactly `pak0` … `pak3`;
     `expansion` = base name is `pak3` (`S:610-616`).
   - Read the zip **central directory** (3.3) and keep entries whose lowercased name is:
     - `maps/<x>.bsp` with exactly two path components → a map named `<x>` (case as stored),
       `pk3` = this file's name, `retail`/`expansion` as above. Stored in `maps[lower(x)]`,
       overwriting earlier ones.
     - `scripts/*.arena`, or exactly `scripts/arenas.txt` → read the entry and parse it (3.4)
       into `arenas[lower(map)]`, overwriting.
     - `levelshots/*.jpg` or `levelshots/*.tga` → `shots[lower(base)] = (pk3 path, entry)`,
       unless one exists already and this one is a `.tga` (so `.jpg` wins, and a later `.jpg`
       replaces an earlier one).
2. Loose files in the same directory:
   - `maps/*.bsp` (extension case-insensitive) → map, `pk3` = none, retail/expansion = NO.
   - `scripts/*.arena` and `scripts/arenas.txt` (case-insensitive) → parse.
   - `levelshots/*.jpg|*.tga` → `shots[lower(base)] = (file path, loose)`, unconditionally.
3. Text decoding (`S:441-449`): UTF-8, falling back to ISO-8859-1.

Then, for each map:

- With **no arena info** and `retail` or `expansion`: **skip it** (the single-player maps share
  the retail paks).
- With arena info: `longName` = arena `longname` with colour codes stripped (7.9) and trimmed;
  `types` = arena `type`, lowercased and trimmed.
- Attach the levelshot location, if any (the image is loaded lazily).

Sort by `displayName` (locale-aware, case-insensitive), then by `name` (case-insensitive).
`displayName` = `longName` if non-empty, else `name`. Log
`Server: <n> Holomatch maps in the game files (<ms> ms)`.

### 3.3 Zip reading (`S:337-439`)

Plain C logic (little-endian helpers `LE16`, `LE32`):

- **End of central directory**: read the last `min(size, 65536+22)` bytes; search backwards
  from `len−22` for `50 4B 05 06`. Not found, or file < 22 bytes → no entries.
  `cdSize = LE32(eocd+12)`, `cdOffset = LE32(eocd+16)`; reject if `cdOffset+cdSize > size`.
- **Central directory records**: while at least 46 bytes remain and the signature is
  `50 4B 01 02`: `n = LE16(+28)` name length, `x = LE16(+30)` extra length, `c = LE16(+32)`
  comment length; stop if the name runs past the end. Name = bytes `+46 … +46+n`, decoded as
  ISO-8859-1. `method = LE16(+10)`, `csize = LE32(+20)`, `usize = LE32(+24)`,
  `offset = LE32(+42)`. Advance by `46+n+x+c`. No zip64.
- **Reading an entry**: refuse if `usize > 32 MiB` or the method is not 0 (stored) or 8
  (deflate). Read the 30-byte local header at `offset`; require `50 4B 03 04`. Data starts at
  `offset + 30 + LE16(local+26) + LE16(local+28)`; read `csize` bytes (all must be read).
  Method 0: return the bytes. Method 8: raw inflate (`inflateInit2(-MAX_WBITS)`) into a
  `usize` buffer with `Z_FINISH`; success on `Z_STREAM_END`, or `Z_BUF_ERROR` with the output
  full; truncate to `total_out`. Uses the engine's bundled zlib.

### 3.4 Arena parsing (`S:540-608`)

Tokeniser (Quake 3 style): skip characters ≤ space; `//` to end of line is a comment;
`/* … */` is a comment (unterminated: to the end); `"…"` is one token without the quotes
(unterminated: to the end); `{` and `}` are single tokens; anything else runs until a character
≤ space or a brace.

Parser: scan tokens; at each `{`, read key/value pairs until `}` (keys lowercased); store the
block under `lower(info["map"])` when `map` is non-empty. Keys used: `map`, `longname`, `type`
(space-separated words such as `ffa tourney team ctf`).

### 3.5 Map info helpers (`S:497-536`)

- `supportsGametype(g)`: empty/unknown `types` → yes. Else split on whitespace and require the
  word: g = 1 `tourney`, 3 `team`, 4 `ctf`, otherwise `ffa`.
- `typesDescription`: empty types → none; else the present ones, in the order "FFA",
  "Tourney", "Team", "CTF", joined by `", "` (none if none present).
- `levelshot`: loaded once, on first use (pk3 entry via 3.3, or the loose file), decoded as an
  image (JPEG or TGA; note Qt's `QImage` has no TGA reader by default — use `stb_image` or a
  small TGA decoder in the core). Failure → no image (not retried).

### 3.6 Caching (`S:630-663`)

A **stamp** string is built from every entry in each directory and in its `maps`, `scripts` and
`levelshots` subfolders: `"<dir>/<file>:<size>:<mtime seconds as float>|"`. The catalog is
recomputed only when the stamp differs from the cached one (one process-wide cache; the stamp
is recomputed on every call).

### 3.7 `VSP_FindMap(maps, name)` (`S:754-760`)

The first map whose `name` equals `name` case-insensitively.

### 3.8 Expansion Pack map list (`S:4552-4562`)

A hard-coded set, used by the browser even when the Expansion Pack isn't installed:
`ctf_breach, ctf_dn1, ctf_for1, ctf_neptune, ctf_oldwest, ctf_reservoir, ctf_singularity,
ctf_spyglass2, ctf_stasis, hm_altar, hm_blastradius, hm_borgattack, hm_for2, hm_raven,
hm_temple, hm_voy3`.

---

## 4. The Server Settings sheet ("Config…", `S:1144-2031`)

Opened by the Host card's "Config…" button (small, tooltip "Maps, game type, bots and the
server's name, password and network", `L:795-798`). Edits a **copy** of the settings; Done
copies the copy back and saves; Cancel discards.

### 4.1 Window (`S:1194-1262`)

- Title "Server Settings". Content width 700 pt; titled only (not resizable, no close button).
- Top: a segmented control, 400 pt wide, equal segments, single selection:
  "Maps" | "Game" | "Server" | "cMod". Selecting a segment switches the page below (the page
  container itself shows no tabs). Initially "Maps".
- Middle: a rounded group box holding the page; the page area is 480 pt tall.
- Footer (left→right): "Restore Defaults", flexible space, "Cancel" (Escape, ≥ 80 pt),
  "Done" (Return/default, ≥ 80 pt).
- Margins 18 top / 20 sides / 20 bottom; 14 pt vertical spacing.

Footer actions:

- **Restore Defaults** (`S:1959-1966`): every one of the 26 keys of the edit copy is set to a
  fresh default settings object's value (including hostname, password and rotation); all pages
  refresh.
- **Done** (`S:1968-1984`): end any active text edit (commit it), set `rotation`, copy all 26
  keys into the original settings, save, log
  `Server settings saved: <hostname>, <gametypeName>, <n> maps`, close with OK.
- **Cancel**: close; nothing saved.
- The launcher's `done(saved)` callback then refreshes the Host summary (called in both cases).

On open (`showAll`, `S:1940-1955`): search cleared, map list filtered, rotation shown, Game page
shown, name/password fields filled, number fields refreshed, checkboxes and Tick Rate set,
Server and cMod pages updated. The catalog is loaded once when the sheet opens (`S:1187`).

### 4.2 Maps page (`S:1264-1583`)

Two equal columns, 20 pt apart, 16 pt side and 14 pt top/bottom insets; both lists end at the
same height.

**Left column**

1. Caption (11 pt semibold, secondary): `"MAPS · <total>"`, or while searching
   `"MAPS · <shown> OF <total>"`.
2. Search field, placeholder "Search maps", 22 pt tall. Filters live (every keystroke) on the
   trimmed query: keep maps whose display name **or** name contains the query (case-insensitive,
   locale-aware).
3. Map list (boxed, scrolling): one column, no header, 50 pt rows, multiple selection, inset
   style, transparent background. Each row is a **map cell** (4.2.1). Double-click a row = add
   the clicked row to the rotation. Rows can be dragged onto the rotation list (copy).
4. Button "Add to Rotation" with a leading `plus` icon (11 pt): appends the selected maps (in
   list order) to the end of the rotation. Enabled only while the map list has a selection.
   After adding, the newly added rotation rows are selected and the last is scrolled into view.
   **Duplicates are allowed** (a map already in the rotation is added again).

**Right column**

1. Caption: `"ROTATION · 1 MAP"` or `"ROTATION · <n> MAPS"` (0 too: "ROTATION · 0 MAPS").
2. Hint (11 pt secondary, wraps at 300): "The server plays these in order, then starts over."
   (9 pt below it instead of 8.)
3. Rotation list (boxed, scrolling): the same look; title is
   `"<index>.  <display name, or the raw name if missing>"` (index from 1, two spaces).
   Multiple selection. Accepts drops: from the map list (copy: the dragged map names are
   inserted at the drop position) and from itself (move: the dragged rows move to the drop
   position, adjusted for rows removed above it). Drop indicator: a gap between rows, always
   "drop above" a row; the drop row is clamped to 0…count. After a drop the inserted rows are
   selected. Delete / Backspace / Forward-Delete removes the selected rows.
4. Button row: `minus` icon button (tooltip "Remove from the rotation"), `chevron.up`
   ("Play earlier"), `chevron.down` ("Play later"), flexible space, label "Drag to reorder"
   (11 pt tertiary). Icon buttons are small rounded buttons with 12 pt icons; the tooltip is
   also the accessibility label.
   - Remove: enabled with a selection; removes the selected rows, then selects the row at the
     first removed index (clamped to the last row).
   - Play earlier: enabled when the selection's first index > 0; moves the selected rows
     (gathered into one contiguous block, in order) so the block starts at `firstIndex − 1`;
     the block stays selected.
   - Play later: enabled when the selection's last index < count − 1; after removing the
     selection the block is inserted at `lastIndex + 2 − selectedCount`; stays selected.

Every rotation change refreshes the caption, both lists (row numbers, detail colours), the
button states and the Game page's game-type hint (`S:1392-1401`).

#### 4.2.1 Map cell (`S:894-944`, `S:1466-1536`)

- Thumbnail 56×42 pt at the left (4 pt inset), vertically centred, corner radius 4, image
  stretched to fill (not aspect-preserving). No levelshot → a 64×48 placeholder filled with the
  group-box colour plus a quaternary tint, with a centred 18 pt symbol at 50 % opacity: `map`
  normally, `questionmark` when the map is missing from the catalog (`S:946-960`).
- Title: 13 pt medium, truncating tail, bottom on the cell's vertical centre, 9 pt right of
  the thumbnail.
- Detail line: 11 pt, single line, truncating tail, top 1 pt below the centre. Built from parts
  separated by `" · "` (secondary colour); coloured parts are 11 pt medium:
  - Map missing from the catalog: `<name>` · `"Missing from your game files"` (red).
  - Otherwise: `<name>`; then `"Expansion Pack"` (purple) if expansion, else `"Add-on"`
    (blue) if not retail; then either `"Not made for <gametypeName of the edited game type>"`
    (orange) when it doesn't support the game type, or its `typesDescription` (plain) when
    there is one.
- Cell tooltip:
  - missing: "Not in any .pk3 file in your game folders any more. The server skips it."
  - expansion: "<name> (in <pk3>). Players need the Expansion Pack: servers never send its files."
  - add-on: "<name> (in <pk3, or "a loose .bsp file">). Players without it can download it if map downloads are on."
  - retail: "<name> (in <pk3>)"

### 4.3 Game page (`S:1585-1777`)

A form grid: label column 112 pt (labels 13 pt, right-aligned), 18 pt from the top, 16 pt from
the left, 8 pt row spacing. Rows:

| # | Label | Control |
|---|---|---|
| 0 | "Game type:" | popup: "Free For All" (0), "Tournament" (1), "Team Holomatch" (3), "Capture the Flag" (4) |
| 1 | — | game-type hint (11 pt, wraps at 420) |
| 2 | "Modifiers:" (top padding 6, top-aligned) | a 2-column sub-grid (column 0 112 pt, 5 pt row spacing, 8 pt column spacing): per modifier a checkbox and a hint (wraps at 370) |
| 3 | — | hint: "Some modifiers exclude others, as in the game's own Create Server menu." |
| 4 | "Frag limit:" (top padding 8) | number field 0–999, unit "points" |
| 5 | "Capture limit:" | number field 0–99, unit "flags" |
| 6 | "Time limit:" | number field 0–999, unit "minutes" |
| 7 | — | checkbox "Friendly fire: teammates can hurt each other" |
| 8 | "Bots:" (top padding 8) | number field 0–31 (no unit), 12 pt gap, skill popup |
| 9 | — | bots hint (11 pt, wraps at 420) |

Modifier rows (the checkbox tooltip and the hint text are the same string):

| Checkbox | Hint / tooltip |
|---|---|
| "Assimilation" | "Killed players join the enemy team, the Borg. Team Holomatch only." |
| "Specialties" | "Players choose a class, each with its own weapons and abilities." |
| "Disintegration" | "One direct hit causes instant death." |
| "Elimination" | "Last one standing wins. Not with Capture the Flag." |
| "Action Hero" | "One random player gets all the weapons and armor. Free For All and Tournament only." |

Skill popup items: `"1 · Cadet"`, `"2 · Ensign"`, `"3 · Lieutenant"`, `"4 · Commander"`,
`"5 · Captain"` (values 1–5).

**Number field** (`S:811-892`): a right-aligned integer text field (50 pt wide; the port field
64 pt) with a formatter limiting min/max and no decimals, a non-wrapping stepper and an
optional unit label (13 pt secondary), 4 pt apart, vertically centred. Behaviour:

- Stepper click → the field shows the value → model set.
- Each keystroke, if the field is non-empty: model = clamp(field's integer, min, max).
- End of editing: field and stepper re-read the model (so invalid text reverts).
- Each model change calls the field's change hook (Game page: `showLimits`; port:
  `showServerTab`).

**Behaviour**

- Game type change (`S:1694-1701`): set gametype, `applyModifierRules(none)`, refresh the page,
  reload both map lists (their "Not made for …" details depend on it).
- Modifier checkbox (`S:1703-1708`): set that modifier, `applyModifierRules(favoring: it)`,
  refresh the page (other checkboxes may turn off).
- Modifier enabling (`S:1757-1768`): Assimilation enabled only for Team (3); Elimination
  disabled for CTF (≥ 4); Action Hero disabled for Team and CTF (≥ 3). The others are always
  enabled.
- `showLimits` (`S:1741-1755`): Frag limit disabled for CTF; Capture limit enabled only for
  CTF; Friendly fire enabled only for Team/CTF (≥ 3); the skill popup enabled only when
  bots > 0. Bots hint: bots = 0 → "No bots."; team games →
  "Bots keep each team at <n> players; they leave as people join."; otherwise
  "Bots keep the game at <n> players; they leave as people join."
- Game-type hint (`S:1671-1692`), over the rotation maps that are in the catalog (missing ones
  are ignored): none → "Add maps to the rotation on the Maps tab." (orange); all supported →
  "The map in the rotation is made for it." (exactly 1) / "All <n> maps in the rotation are
  made for it." (secondary); otherwise "<made> of the <total> maps in the rotation are made
  for it; the others may lack spawn points or flags." (orange).

### 4.4 Server page (`S:1779-1865`)

Form grid, label column 112 pt:

| # | Label | Control |
|---|---|---|
| 0 | "Server name:" | text field, 300 pt |
| 1 | "Password:" | text field, 200 pt, placeholder "None" (plain text, not masked) |
| 2 | — | hint: "Players type it in Holomatch's Join menu. Leave it empty for an open server." |
| 3 | "Max players:" (top padding 6) | number field 2–32, unit "including bots" |
| 4 | "Network:" (top padding 6) | radio "Local network only" |
| 5 | — | radio "Internet: also list it on the master server" |
| 6 | — | checkbox "Open the port on my router automatically" (leading, no top padding); tooltip "Asks your router (NAT-PMP / UPnP) to forward the port while the server runs, so you don't have to set up port forwarding." |
| 7 | "Port:" | number field 1024–65535 **without a stepper** (64 pt), then the label "UDP · 27960–27963 show up in the Join menu" (11 pt secondary), 8 pt apart |
| 8 | — | network hint (11 pt, wraps at 400) |
| 9 | "Downloads:" (top padding 6) | checkbox "Let players download maps they don't have" |
| 10 | — | hint: "Add-on maps only: the retail and Expansion Pack files are never sent." |
| 11 | "Files:" | checkbox "Pure server: players must use the same game files" |
| 12 | "Tick Rate:" (top padding 6) | popup "Standard (20)" (20) / "High (40)" (40); tooltip "How many times a second the server updates the game (sv_fps). High uses more upload bandwidth." |
| 13 | — | hint: "Updates per second. High uses more upload bandwidth." |

- The name and password fields write the model on every keystroke (through the cleaning
  setters, 2.2).
- Radios: Internet sets `internet = YES`, Local sets NO; the page refreshes.
- "Open the port…" is enabled only when Internet is selected (its state is kept either way).
- Network hint (`S:1849-1865`), with `LANDiscoverable(port)` = 27960 ≤ port ≤ 27963 (`S:116-120`):
  - not discoverable (orange): "Outside 27960–27963, Holomatch's Join menu won't find the server on your network: players have to connect by address."
  - internet + openPort (secondary): "The server window shows your public address once the router has opened UDP port <port>. If it can't, forward that port to this Mac by hand."
  - internet without openPort: "Players outside your network need UDP port <port> forwarded to this Mac on your router."
  - local: "Players on your network find it in Holomatch's Join menu."
- The port field's change hook refreshes this hint live.

### 4.5 cMod page (`S:1867-1938`)

Form grid, label column **128** pt:

| # | Label | Control |
|---|---|---|
| 0 | "Game code:" | checkbox "Run cMod's game code"; tooltip "cMod's server game module (qagame) by Noah Metzger, in place of the original one (sv_cmodGame)." |
| 1 | — | cMod hint (wraps at 420) |
| 2 | "Lag compensation:" (top padding 10) | popup "Off" (0) / "Instant-hit weapons" (1) / "All weapons" (2); tooltip "g_unlagged: the server checks each shot against where the shooter saw the other players." |
| 3 | — | hint: "Shots hit what the shooter saw on screen, so players with a high ping can aim normally. Works for every player, whatever game they run. Instant-hit weapons is the common choice; All weapons also covers projectiles." |
| 4 | — | checkbox "Smooth other players' movement"; tooltip "g_smoothClients" |
| 5 | — | checkbox "Let cMod players see their own shots at once"; tooltip "g_unlaggedPredict: players with cMod's cgame (this app's Holomatch, cMod) predict their weapon fire." |
| 6 | — (top padding 10) | hint: "cMod is by Noah Metzger (Chomenor). Players never download it from your server." |

State (`S:1923-1938`), with `have = VSP_CmodAvailable()` and `on = have && cmodGame`:

- "Run cMod's game code": checked = `cmodGame`; enabled = `have`.
- cMod hint: have → "cMod's version of the Holomatch game: the original rules and modes, many
  bug fixes, and lag compensation." (secondary); else "cMod's game modules aren't in this copy
  of the app." (orange).
- Lag compensation popup enabled = `on`; Smooth enabled = `on`; Predict enabled =
  `on && unlagged > 0`.
- Any change on this page re-runs this.

### 4.6 Snapshot hook (`S:2018-2031`)

`VSP_SnapshotServerConfig(settings, dataFolder, tab, path)` builds the sheet off screen, selects
`tab` (`maps|game|server|cmod`, case-insensitive), optionally picks a Tick Rate
(`VOYAGERSP_TEST_SERVER_TICKRATE`, then presses Done) and renders it to a PNG. Port only if the
Windows/Linux test harness wants it; not user-visible.

---

## 5. Generated cfg files

### 5.1 `vsp_server.cfg` (`S:2089-2169`)

Written to `HomeBase/vsp_server.cfg` (UTF-8, atomically; HomeBase created if needed) at **each
server start**, then run by the child with `+exec vsp_server.cfg`. If writing fails, the server
window logs (red) `Couldn't write <path>.` and shows Failed · "couldn't start".

Rotation resolution comes first:

1. For each rotation name, look it up in the catalog (case-insensitive): found → use the
   catalog's `name` (its spelling); not found → log `Server: map <n> is not in the game files, skipped`.
2. If none remain: take the **first catalog map (catalog order) that supports the game type**
   and log `Server: no maps in the rotation, using <map>`. If there is none, fail with
   "There are no Holomatch maps in your game files." (red in the log; Failed state).

Exact content (booleans are `0`/`1`; `<…>` substituted):

```
// Written by the Elite Force launcher (Host a Server) at each start.
// Change these settings in the launcher's Config… sheet; edits here are overwritten.

set sv_hostname "<hostname>"
set g_password "<password>"
set sv_maxclients <maxPlayers>
set sv_allowDownload <allowDownload>
set sv_pure <pure>
set sv_fps 40

// cMod's game code (by Noah Metzger) and its lag compensation
set sv_cmodGame <cmodGame>
set g_unlagged <unlagged>
set g_smoothClients <smoothClients>
set g_unlaggedPredict <unlaggedPredict>

set g_gametype <gametype>
set g_pModAssimilation <assimilation>
set g_pModDisintegration <disintegration>
set g_pModActionHero <actionHero>
set g_pModSpecialties <specialties>
set g_pModElimination <elimination>
set fraglimit <fraglimit>
set capturelimit <capturelimit>
set timelimit <timelimit>
set g_friendlyFire <friendlyFire>

// bots fill the game up to this many players (per team in team games)
set bot_enable 1
set bot_minplayers <bots>
set g_spSkill <botSkill>

// the master servers it announces itself to (the launcher's Servers… sheet)
set sv_master1 "<master 1>"
set sv_master2 "<master 2>"
… (16 lines in all; empty strings past the end of the list)
set sv_master16 ""

// the map rotation
set vsp_map1 "map <map1> ; set nextmap vstr vsp_map2"
set vsp_map2 "map <map2> ; set nextmap vstr vsp_map3"
…
set vsp_mapN "map <mapN> ; set nextmap vstr vsp_map1"
vstr vsp_map1
```

Exact details (`S:2121-2160`):

- `set sv_fps 40` is present **only** when `tickRate == 40` (Standard writes nothing and leaves
  the engine default; the engine's `sv_minSnaps` gives stock clients every update).
- The blank line after the `sv_pure`/`sv_fps` group is always there.
- The masters block is `VSP_MastersCfgText(blank)` (5.2); `blank` is true only with the test
  hook `VOYAGERSP_TEST_SERVER_NOMASTER`. It is preceded by its comment line and followed by
  `"\n// the map rotation\n"` (so one blank line separates them).
- Rotation line `i` (1-based) points to `i+1`; the last points back to 1; a one-map rotation
  points to itself.
- The cfg uses `set`, not `seta`.

Value safety: hostname/password were cleaned of `" ; \ %` and control characters (2.2); map
names come from pk3 entry names (a name with a space or `;` would break the line; not guarded).

### 5.2 Masters text and `vsp_masters.cfg` (`S:3904-3924`)

`VSP_MastersCfgText(blank)`: for i = 1 … 16, `set sv_master<i> "<list[i−1], or empty>"\n`,
from the saved list (section 6), or all empty when `blank`.

`VSP_WriteMastersConfig()` writes `HomeBase/vsp_masters.cfg` (UTF-8, atomic; HomeBase
created):

```
// Written by the Elite Force launcher at each start: its Servers… list.
// sv_master1-5 are Holomatch's Internet 1-5. Edits here are overwritten.

set sv_master1 "…"
… (16 lines)
```

On failure it returns NO and logs `Couldn't write <path>`; callers then leave out the `+exec`.
It is written every time a Holomatch client command line is built (Play in Holomatch mode,
and the server instance's `clientArgs`). All 16 slots are always written, blanks included, so a
removed master doesn't linger in the client's archived config. (A file is used because 16
`+set`s don't fit the engine's 1024-character command line.)

### 5.3 Engine-side files (not written by the launcher)

The dedicated server reads/writes `hmconfig_server.cfg` (not the player's `hmconfig.cfg`) and
uses the PID file `Lilium Voyager SP_server.pid` (docs/macos-port-notes.md, "Dedicated
server"). The launcher does not touch them.

---

## 6. Master servers and the Master Servers sheet ("Servers…", `S:3841-4465`)

### 6.1 The list model

- Up to **16** masters (`VSP_MAX_MASTERS`, = engine `MAX_MASTER_SERVERS`, `H:113`,
  `qcommon/q_shared.h:159`). Slot *i* is `sv_master<i>`. The first five are Holomatch's Join
  menu sources **Internet 1–5** (`MASTER_SLOTS = 5`, `S:3846`); slots 6–16 are
  "announce only": a hosted internet server heartbeats to them, but the retail menu can't
  browse them.
- **Recommended defaults** (`VSP_DefaultMasters`, `S:3848-3853`; must match the engine's
  `EF_DEFAULT_MASTERS`, `qcommon/qcommon.h:287`):
  1. `master.stef1.daggolin.de`
  2. `efmaster.tjps.eu`
  3. `master.stvef.org`
  4. `master.stef1.ravensoft.com`
  5. `ipv6.tjps.eu`
- **Original defaults** (`VSP_OriginalMasters`, `S:3855-3859`): `master.stef1.ravensoft.com`,
  `efmaster.tjps.eu` (slots 3–5 blank).
- **CleanMaster(text)** (`S:3861-3874`): trim whitespace, lowercase, keep only characters in
  `abcdefghijklmnopqrstuvwxyz0123456789.-:[]_`, at most 64 characters kept.
- **CleanMasters(list)** (`S:3876-3887`): CleanMaster each (non-strings → empty), drop empty
  ones and repeats (first occurrence wins), keep at most 16.
- **VSP_MasterServers()** (`S:3889-3893`): if the store has an array under **`MasterServers`**,
  `CleanMasters(it)` (which may be empty: the user removed them all); otherwise the recommended
  defaults. Read **live** from the store on every call (the server window and the browser call
  it at different times).
- **VSP_SetMasterServers(list)** (`S:3895-3902`): if `list` is nil or `CleanMasters(list)`
  equals the recommended defaults exactly (same order), **remove** the key (so the user
  follows future default changes); otherwise store `CleanMasters(list)`. Logs
  `Master servers: <list joined by ", ">`.
- **Address forms** accepted: `host`, `host:port`, `[ipv6]:port`, `[ipv6]`; default port
  27953 (`PORT_MASTER`). Parsing is in 7.1.
- **VSP_QueryMasterCount(master)** (`S:3926-3930`): `QueryMaster` (7.1); the number of
  distinct `ip:port` entries, or −1 when it didn't answer. Blocking (≈2 s after the last
  packet).

### 6.2 Sheet layout (`S:4019-4100`)

- Title "Master Servers"; titled only; content width 660 pt (initial rect 660×470).
- Root column, margins 18 top / 20 sides / 20 bottom, 8 pt spacing (10 after the hint, 18
  after the button row). From top:
  1. Caption (11 pt semibold, secondary): `"MASTER SERVERS · 1"` / `"MASTER SERVERS · <n>"`.
  2. Hint (11 pt secondary, wraps at 610): "Master servers keep the list of Holomatch games on
     the internet. The first five are the Join menu's Internet 1 to 5 sources, in this order.
     When you host an internet server, it announces itself to every one, so add new masters
     here as the community starts them."
  3. The master list, boxed and scrolling, **280 pt tall**: one column, no header, 30 pt rows,
     inset style, transparent background, multiple selection.
  4. Button row: `plus` (tooltip "Add a master server"), `minus` ("Remove"), `chevron.up`
     ("Move up"), `chevron.down` ("Move down") — small rounded icon buttons, 12 pt icons,
     tooltip = accessibility label; 12 pt gap; label "Drag to reorder, double-click to edit"
     (11 pt tertiary); flexible space; small button "Check Now" (tooltip "Ask each master
     server for its list of games").
  5. Footer: "Restore Recommended Defaults" (tooltip
     `"The live community masters first: "` + the five defaults joined by `", "`),
     "Restore Original Defaults" (tooltip "As the game came: Internet 1 Raven's master (no
     longer running), Internet 2 efmaster.tjps.eu, 3 to 5 blank"), flexible space,
     "Cancel" (Escape, ≥ 80 pt), "Done" (Return, ≥ 80 pt).

### 6.3 Row (`VSPMasterCell`, `S:3932-3977`, `S:4173-4219`)

A horizontal row, 6 pt side insets, 8 pt spacing, vertically centred:

1. **Slot label** (12 pt secondary, 92 pt wide): rows 0–4 `"Internet <row+1>"`, tooltip
   "Holomatch's Join menu lists this master's games under Internet <row+1>"; rows ≥ 5
   `"Announce only"`, tooltip "Not in the Join menu (it has Internet 1 to 5), but a server you
   host announces itself here too". Only this label depends on the row; it is redone after
   every move.
2. **Address** (13 pt medium, borderless, no background, no focus ring, truncating tail,
   placeholder "host or host:port"). Normally **not editable or selectable** (a click selects
   or drags the row); double-click makes it editable (6.4).
3. **Status icon** (12 pt) and **status text** (11 pt, right-aligned, 128 pt wide):
   - checking: `arrow.triangle.2.circlepath` (secondary), "Checking…"
   - answered with *n* ≥ 0: `checkmark.circle.fill` (green), "Answers · 1 game" /
     "Answers · <n> games" (secondary text)
   - no answer (−1): `xmark.circle.fill` (orange), "No answer" (orange text), tooltip
     "It didn't answer from this network. It may be down, or reachable only over IPv6. Games
     it lists still show up for players who can reach it."
   - not checked yet: no icon, empty text.
   A status update must not overwrite the address while it is being edited.

### 6.4 Behaviour (`S:3997-4443`)

State: `masters` (editable copy of `VSP_MasterServers()`), `answers` (address → count or −1),
`checking` (set of addresses being queried), `editing` (the field being edited) and
`editingWas` (its address when editing began).

- **On open**: every master is checked at once (`checkAllWaiting(NO)`, `S:4445-4455`).
- **check(m)** (`S:4123-4143`): skip empty addresses and ones already being checked; mark
  checking, forget the old answer, reload its row; on a background thread
  `VSP_QueryMasterCount(m)`; back on the UI thread: unmark, store the answer, log
  `Master <m>: no answer` or `Master <m>: <n> servers`, reload the row **found by address**
  (it may have moved).
- **Check Now**: end any edit, then check every master (those already in flight are skipped).
- **Selection-driven enabling** (`S:4110-4117`): `+` enabled while count < 16; `−` with a
  selection; up when the first selected index > 0; down when the last selected index < count−1.
- **Add** (`S:4279-4287`): if count < 16: end any edit, append an empty row, begin editing it.
- **Double-click** a row: begin editing it (`S:4221-4242`): end any other edit first (which may
  move rows), find the row again by its address, select and scroll to it, make the address
  editable/selectable, focus it (all text selected).
- **End of editing** (`S:4246-4277`; Return, Tab, clicking elsewhere): `m = CleanMaster(text)`;
  find the row by `editingWas` (not by index). Then:
  - `m` empty → remove the row (an added row left empty disappears).
  - `m` already elsewhere in the list → system beep, keep the old address (if the row was a new
    empty one, remove it).
  - else replace the row's address with `m`.
  - Refresh; if `m` is in the list and has no answer yet, check it.
- **Remove** (`S:4289-4304`; also Delete / Backspace / Forward-Delete in the list): end any
  edit, re-read the selection, remove the selected rows, select the row at the first removed
  index (clamped).
- **Move up/down** (`S:4306-4324`): end any edit; move the selected rows as one block to above
  `firstIndex − 1` (up) or to above `lastIndex + 2` (down) using **moveRows**.
- **moveRows(from, toAbove row)** (`S:4326-4352`): take the rows, remove them, insert them at
  `row − (number of moved rows above row)`, clamped; animate the table rows moving; relabel the
  slot labels of all visible rows; select the moved rows; update buttons.
- **Drag to reorder** (custom table, `S:962-1078`): a plain click (no Command/Shift) on a row
  selects it and, once the pointer moves ≥ 3 pt, lifts a snapshot of the row (25 % shadow,
  radius 6, offset (0,−2)) that follows the pointer vertically, clamped to the rows; whenever
  the row under the snapshot's middle changes, the model row moves there **live**
  (`moveRows([from], to > from ? to+1 : to)`), the slot under the pointer is left blank
  (alpha 0) and other rows slide; the view autoscrolls. On release the snapshot animates
  (0.15 s) into its slot. Double-click (click count > 1) on a row is detected here too and
  begins editing on mouse-up. Command/Shift clicks are normal multi-select. Qt: use
  `QAbstractItemView::InternalMove` or a custom live reorder; Win32: a ListView with custom
  drag handling. Live reordering with the slot labels updating is the expected feel.
- **Restore Recommended Defaults / Restore Original Defaults** (`S:4417-4428`): end any edit;
  replace the list; refresh; check every master without an answer yet.
- **Done** (`S:4430-4438`): end any edit (commits it), `VSP_SetMasterServers(masters)`, close
  with OK. **Cancel**: close, nothing saved. The launcher refreshes its summary either way.

### 6.5 Snapshot hook (`S:4457-4465`, `S:4354-4413`)

`VSP_SnapshotMasterServers(path)` runs test actions from `VOYAGERSP_TEST_MASTERS_ACTIONS`
(`edit<row>`, `type:<text>`, `drag<row>to<row>`, `dclick<row>`, `select<row>`, `add`, `end`),
logging `[test] <action> -> <list joined by " | "> [(editing)] | shown: <slot=address, …> |
selected <indexes>` after each, then checks every master, **waiting** until all answered,
and renders the sheet.

---

## 7. Network protocols (byte level)

All UDP. Out-of-band packets start with four `0xFF` bytes. Strings below are sent **without**
a trailing NUL (the length is `sizeof(literal) − 1`).

### 7.1 Master query: `getservers` (`QueryMaster`, `S:3086-3136`)

**Address parsing** of a master string:

- Starts with `[`: the host is between `[` and the first `]` (no `]` → fail). If the character
  right after `]` is `:` and something follows, that is the port.
- Otherwise, if the string contains **exactly one** `:`, split host/port there.
- Otherwise (no colon, or a bare IPv6 address with several colons) the whole string is the
  host.
- Default port `"27953"`.
- Resolve with `getaddrinfo(host, port, {family = AF_UNSPEC, socktype = SOCK_DGRAM})`; use the
  **first** result only (so an IPv6-only master like `ipv6.tjps.eu` works where IPv6 works).
  Resolution failure → no answer.

**Request** — one datagram:

```
FF FF FF FF  "getservers 24 full empty"
```

`24` is Elite Force 1.2's protocol number; `full` asks for full servers, `empty` for empty
ones, i.e. everything. Sent once, no retry.

**Receive loop**: socket receive timeout 2 s (`SO_RCVTIMEO`); `recv` into a 16 KiB buffer
until a receive times out or fails. Every datagram received is parsed into the same set. The
result is **nil (no answer)** if no datagram arrived at all, otherwise the set (possibly
empty). The datagram source is not checked and the `getserversResponse` header is not checked
(the parser just looks for the first `\`). Total time: 2 s after the last packet.

**Response parsing** (`ParseServerList`, `S:3048-3084`, plain C). Two encodings exist; both are
a run of entries, each starting with `\`, terminated by `\EOT`:

- **Binary (Quake 3)**: `\` + 4 address bytes (network order) + 2 port bytes (big-endian),
  i.e. 7 bytes per entry.
- **Hex text (the older Elite Force master, e.g. efmaster.tjps.eu)**: `\` + 12 hexadecimal
  digits (8 for the address, 4 for the port, each byte as two digits, upper or lower case),
  i.e. 13 bytes per entry.

Algorithm:

```
p = first '\' in buf; none → return
hex = (p+14 <= end) && p[13] == '\' && p[1..12] are all hex digits    // decided once
while p && p+7 <= end && memcmp(p+1, "EOT", 3) != 0:
    if hex:
        if p+13 > end: break
        v[i] = hexbyte(p[1+2i], p[2+2i])  for i in 0..5 ; p += 13
    else:
        v[i] = p[1+i]                      for i in 0..5 ; p += 7
    add "v0.v1.v2.v3:(v4<<8|v5)" to the set
    if p < end && *p != '\': p = next '\' after p (or NULL)       // tolerate stray bytes
```

Entries are kept as dotted-quad `a.b.c.d:port` strings in a **set** (duplicates across packets
and masters merge). IPv6 server entries are not supported (EF masters list IPv4 servers).

### 7.2 Server info: `getinfo` (`QueryServerInfo`, `S:4678-4777`; `ParseInfoResponse`, `S:4642-4671`)

**Request**: `FF FF FF FF "getinfo browse"` (the challenge is the literal word `browse`; it is
not checked).

**Response**: `FF FF FF FF "infoResponse"` followed by `\n` (in practice) and an info string
`\key\value\key\value…`. Parsing:

- Require length ≥ 17 and the 16-byte prefix `FF FF FF FF infoResponse`.
- Find the first `\`; decode from there to the end as ISO-8859-1; split on `\`; pairs from
  index 1: `kv[lowercase(key)] = value`.
- Fields:
  - `hostname` → the raw name with colour codes (empty if missing);
    `name` = colour-stripped (7.9) and trimmed; if `name` is empty, both become the address.
  - `mapname` → `map`, **lowercased** (empty if missing).
  - `clients` → `players` (integer, 0 if missing). This **includes bots**.
  - `sv_maxclients` → `maxPlayers`.
  - `g_humanplayers` → `humans` (people without bots), or **−1** when the key is absent.
  - `gametype` → `gametype` (integer).
  - `needPass` = `g_needpass` ≠ 0 **or** `needpass` ≠ 0.
- The server address is the datagram's source `ip:port` (not taken from the payload).

### 7.3 Server status: `getstatus` (`ParseStatusResponse`, `S:4798-4845`)

**Request**: `FF FF FF FF "getstatus"`.

**Response**: `FF FF FF FF "statusResponse"` then lines separated by `\n`:

- Require length ≥ 19 and the 18-byte prefix; the rest (from byte 18, i.e. starting with the
  `\n` after the header) is decoded as ISO-8859-1 and split on `\n`.
- A line starting with `\` is an info string (`\key\value…`, keys lowercased).
- Every other line is a player: `<score> <ping> "<name>"`. Parse two integers (skipping
  whitespace), then require a `"`; the name is everything after the first `"`, with `"` and
  `\r` trimmed from both ends (it keeps its colour codes). Lines that don't parse are ignored
  (e.g. the empty last line).
- `humans` = `g_humanplayers` or −1.
- **cMod detection** (`S:4838-4842`): `cmodVersion` = the value of `version_cmod` when
  non-empty; else `""` (cMod, version unknown) when `version` starts with `"cMod HM"` or
  `"ioEF:cMod HM"`; else none. (`g_unlagged` is deliberately not used: other mods have it.)
- `when` = the time the answer was parsed.

### 7.4 Batch query: info + status on one socket (`QueryServerInfo`, `S:4678-4777`)

Runs on a background thread. Inputs: a list of `ip:port` strings, a `broadcast` flag (Local), a
cancellation flag, and three callbacks delivered on the UI thread (`got(info)`,
`gotStatus(address, status)`, `done(answered)`), each suppressed once cancelled.

1. One IPv4 UDP socket (`AF_INET`, `SOCK_DGRAM`). Failure → `done(0)`.
2. **Broadcast** (Local only): enable `SO_BROADCAST`; send `getinfo browse` to
   `255.255.255.255` on ports **27960, 27961, 27962, 27963** (`PORT_SERVER` … +3).
3. For each address that parses (`ParseAddress`, 7.6): remember `sent[address] = now`, send
   `getinfo browse`. After every 32 sends, sleep 2 ms ("don't flood a home router").
4. `deadline = now + 1.5 s` (Local) or `+ 2.5 s` (masters) (`BROWSER_WAIT_LOCAL`,
   `BROWSER_WAIT`, `S:4549-4550`).
5. Loop while **not cancelled** and `now < deadline + (statusAsked non-empty ? 1.0 s : 0)`
   (`BROWSER_WAIT_STATUS`, `S:4676`) and (`broadcast` or `answered < asked` or `statusAsked`
   non-empty):
   - `poll` 100 ms; on a datagram, `recvfrom` (16 KiB buffer); key = source `"a.b.c.d:port"`.
   - If the key is in `statusAsked` and the datagram parses as a status response: remove it
     from `statusAsked`, deliver `gotStatus(key, status)`; continue.
   - Parse as an info response; not one → ignore.
   - **Ping** = `(now − sent[key]) × 1000` ms (integer), or `now − start` when the key wasn't
     sent to directly (broadcast answers).
   - If the key was sent to: forget it (one answer each) and count `answered++`. Else, if not
     broadcast, ignore the datagram (an unexpected source). Else (broadcast) `answered++`.
     (A broadcast server answering twice would be counted and delivered twice; harmless.)
   - Deliver `got(info)`.
   - If no `getstatus` is out for this key yet: add it to `statusAsked` and send
     `FF FF FF FF getstatus` to it (from the same socket, so its answer comes back here).
     This is how the cMod column and the player list are filled without extra waits.
6. Close the socket; deliver `done(answered)`.

Note: `asked` counts only addresses actually sent to, so with a master list the loop ends as
soon as every server answered its getinfo and every getstatus came back, or at the deadline.

### 7.5 Single status query (`QueryServerStatus`, `S:4847-4879`)

For the player list of the selected server (8.6). Background thread; one IPv4 socket; up to
**2 tries**: send `getstatus`, wait up to **1 s** (polling every 100 ms) for a datagram that
parses as a status response (the source is not checked); stop at the first one. The callback
(UI thread) gets the status or nil.

### 7.6 Server address parsing (`ParseAddress`, `S:4629-4640`)

IPv4 only: split on the **last** `:`; no colon → port 27960 (`PORT_SERVER`). Valid when
`0 < port < 65536` and the host is a dotted-quad that `inet_pton(AF_INET)` accepts. Host names
are **not** resolved here (master lists only contain IPs; the browser's typed address goes to
the engine's `connect` instead, 8.8).

### 7.7 Ping measurement

There is no separate ping: the ping shown is the `getinfo` round trip measured in 7.4 (send
time recorded per address just before `sendto`, receive time when the datagram is read on the
background thread). It includes the 2 ms pacing per 32 sends. For Local broadcast answers it
is the time since the broadcast. Colour thresholds are in 8.4.

### 7.8 HTTP fetches (efservers.com)

Only the browser uses HTTP (8.7). All are plain HTTPS GETs with the platform's default
session (no custom headers, cookies, or timeouts beyond the default):

| Purpose | URL | Response handling |
|---|---|---|
| 3D view lookup (once per map) | `https://efservers.com/getPK3info.php?pk3=<map>&preview` | body as UTF-8 HTML; search for the regex `bsp/index\.php\?pk3=([A-Za-z0-9_.+-]+)&(?:amp;)?file=(maps/[A-Za-z0-9_.+-]+\.bsp)`; a match → the 3D URL `https://efservers.com/bsp/index.php?pk3=<group 1>&file=<group 2>`; a body without a match → "no 3D view" (cached); no body (network error) → not cached, retried next time |
| Map Info page (shown in a web view) | `https://efservers.com/pk3.php?preview&popup=1&map=<map>` | displayed |
| 3D Map Preview (shown in a web view) | the 3D URL above | displayed |
| Globe button | `https://efservers.com/` | opened in the default browser |

`<map>` is percent-encoded with **only ASCII letters and digits left unescaped** (so `_`
becomes `%5F`). The map name is the server's `mapname`, lowercased.

### 7.9 Quake 3 colour codes (`DecodeColors`, `StripColors`, `S:451-482`)

`DecodeColors(s, colors?)` returns `s` without colour codes and, optionally, a per-character
colour index (0–7; 7 = white until a code). Rule: walking the input, when the current
character is an ASCII **letter or digit** and the **last character already output** is `^`,
remove that `^`, set the colour to `(ch − '0') & 7`, and output nothing for `ch`. Otherwise
output the character with the current colour. Because it looks at the output, a `^` left next
to an alphanumeric once a code is removed makes a code too: `"^^071v1"` → `"1v1"` in white
(not `"^71v1"`), as in the game. Letters map via `& 7` too (e.g. `^a` → 1). `StripColors` is
`DecodeColors` without the colours, skipped when there is no `^`.

Colour index → display colour (`ColoredName`, `S:4881-4906`): 0 black → *secondary*, 1 red,
2 green, 3 yellow, 4 blue, 5 cyan → *teal*, 6 magenta → *pink*, 7 white → *label* (normal text
colour), so names read in light and dark themes. The coloured name is then trimmed of leading
and trailing whitespace.

### 7.10 LAN address (`VSP_LANAddress`, `S:2045-2074`)

The IPv4 address friends on the network connect to. Over all interfaces (`getifaddrs`):
skip non-IPv4, not up, not running, loopback, and link-local 169.254/16. Prefer the first
address of `en0`; otherwise the first address of any interface whose name starts with `en`;
otherwise the first of any other interface whose name doesn't start with `utun` or `bridge`.
None → nil (callers show `127.0.0.1`). Windows: `GetAdaptersAddresses` (skip loopback,
tunnel, down, APIPA), preferring the adapter with the default route; Linux: `getifaddrs`,
preferring the interface of the default route (`/proc/net/route`), skipping `lo`, `tun*`,
`tailscale*`/`wg*`, `docker*`, `br-*`, `virbr*`, `veth*`.

### 7.11 Router port mapping and public address (NAT-PMP / PCP / UPnP)

macOS uses `DNSServiceNATPortMappingCreate` from `dns_sd` (mDNSResponder), which speaks
NAT-PMP and PCP (and UPnP IGD on recent macOS) to the default gateway, renews the lease while
the reference is alive, and deletes the mapping when it is released. Two uses:

1. **Address-only request** (protocol 0, ports 0, ttl 0): nothing is opened; the callback
   reports the router's external IPv4 address. Used by `VSP_StartPublicAddressLookup`
   (section 10) and the server window's listing check (9.10).
2. **UDP mapping** (`S:2878-2918`): protocol UDP, internal port = the bound server port,
   **suggested external port = the same port**, ttl 0 (= the daemon's default lease, renewed
   automatically). The callback reports `(error, externalAddress, externalPort)`; external
   port 0 means "same as internal".

Windows/Linux have no system service for this, so the portable core must implement it.
Minimum: **NAT-PMP (RFC 6886)** to the default gateway's UDP port 5351; better: also **PCP
(RFC 6887)** and **UPnP IGD** (e.g. via miniupnpc), to match what mDNSResponder covers.
NAT-PMP wire format:

```
public address request : 00 00                          (version 0, opcode 0)
public address response: 00 80 RR RR EE EE EE EE AA AA AA AA
                          (opcode 128, result u16, epoch u32, external IPv4)
UDP map request        : 00 01 00 00 IP IP EP EP LL LL LL LL
                          (opcode 1, reserved, internal port u16, suggested external
                           port u16, lifetime u32 seconds; 7200 recommended; 0 = delete)
UDP map response       : 00 81 RR RR EE EE EE EE IP IP EP EP LL LL LL LL
                          (opcode 129, result, epoch, internal port, mapped external port,
                           granted lifetime)
all integers big-endian; retransmit after 250 ms, doubling, up to 9 tries (≈64 s)
result codes: 0 success, 1 unsupported version, 2 not authorized/refused,
              3 network failure, 4 out of resources, 5 unsupported opcode
```

Renew at half the granted lifetime while the server runs; send a lifetime-0 request for the
same internal port when it stops. Map the outcomes onto the three conditions the UI
distinguishes (9.9): **unsupported** (no answer / result 1 or 5, or no gateway),
**disabled** (result 2), **double NAT** (an external address that is itself private, or
mDNSResponder's `DoubleNAT`), plus "other error <n>". Gateway discovery is platform code
(Windows `GetBestRoute2`/`GetIpForwardTable2`; Linux `/proc/net/route` or netlink).

**Private address test** (`PrivateIPv4`, `S:2871-2876`, plain C) on a host-order IPv4 `a`:
`a>>24 == 10` or `a>>20 == 0xAC1` (172.16/12) or `a>>16 == 0xC0A8` (192.168/16) or
`a>>22 == 0x191` (100.64/10, carrier-grade NAT).

### 7.12 Firewall

The macOS code does nothing about firewalls (the app firewall prompts for the executable on
its first listening socket; "Local Network" privacy permission is needed for LAN discovery,
see docs/macos-port-notes.md). On **Windows**, the dedicated child binds UDP and triggers the
Windows Defender Firewall prompt for the game executable; the installer should add an inbound
UDP rule for the game executable (private + public profiles) or the server window should
explain the prompt, otherwise LAN and internet clients can't reach it and the listing check
(9.10) will report "not on the server list". On **Linux**, nothing by default (ufw/firewalld
users must open the port; the "forward UDP port" hints apply). Broadcast discovery (7.4) also
needs outgoing broadcast allowed.

---

## 8. The server browser ("Browse…", `VSPBrowserController`, `S:4539-6036`)

Opened from the Holomatch card's "Browse…" button (small; tooltip "See the games on your
network and the internet, and join one", `L:801-804`). It is a sheet on the launcher window.
It returns `(address, password)` only when the user picks a server; Cancel returns nothing.

Design summary (`S:4543-4547`): Holomatch's own Join menu waits for each source in turn. The
browser instead asks a master for its list, then **every** server on it for its info at once
on one socket, in the background, and fills the table as answers come in. Local broadcasts on
the LAN ports as the game does. Combined asks every master at once and merges their lists.

### 8.1 State (`S:5016-5044`, `S:5046-5073`)

Built when the sheet opens:

- `masters` = `VSP_MasterServers()` (a snapshot for the sheet's lifetime).
- `haveMaps` = lowercased names of all catalog maps (`VSP_MapCatalog(dataFolder)`).
- `builtinMaps` = the Expansion Pack list (3.8) ∪ lowercased names of catalog maps that are
  retail or expansion (efservers.com has no page for these).
- `favorites` = the set from the stored array **`BrowserFavorites`** (addresses).
- Per source (index): `results` (servers found), `status` (status-line text), `fetches`
  (the running fetch, with a cancel flag).
- `statuses` (address → last getstatus answer), `asking` (address of the outstanding
  single-status query), `views3D` (map → 3D URL or "none"), `checking3D` (map being looked up).

### 8.2 Window layout (`S:5096-5290`)

- Title "Holomatch Servers"; titled only; content width **780 pt**; fitted height.
  Margins 18 top / 20 sides / 20 bottom; 8 pt spacing (12 after the server list, 4 after the
  players header, 18 after the players box and after the link row).
- Initial focus: the server table.

Rows, top to bottom:

1. **Top row**: the **source tabs** (segmented control, single selection), flexible space,
   **globe** button (rounded, `globe` 12 pt; tooltip "Open efservers.com: who's playing where,
   server and map history"; accessibility "Open efservers.com"), **refresh** button (rounded,
   `arrow.clockwise` 12 pt; tooltip "Ask again"; accessibility "Refresh"; shortcut
   **Command-R** — Ctrl+R on Windows/Linux, F5 also reasonable).
   - Segments: `"Local"`; then, if there is at least one master, `"Combined"`; then one per
     master: `"Internet <n>"`, or just `"<n>"` when there are **more than 7** masters.
   - Control tooltip: "Local: this network. Combined: every master server's list. Internet 1,
     2…: one master server each, in the order set in Servers…". Per-segment tooltips: Local
     "Your network"; Combined "Every master server's list, together"; each master its address.
   - Source index = segment index: 0 Local, 1 Combined, 2 + i = master i.
2. **Status row**: small spinner (shown only while the current source's fetch runs), status
   text (11 pt secondary, truncating tail), flexible space, small checkbox "Only favorites"
   (tooltip "Star servers in the list to make them favorites"), 14 pt, small checkbox "Only
   servers with people" (tooltip "Hide servers with only bots, or no one. Servers that don't
   say (?) stay when they have players"). Checkbox states come from `BrowserOnlyFavorites` and
   `BrowserOnlyPeople` (absent = off).
3. **Server table** (boxed, scrolling, **280 pt** tall): full-width style, transparent
   background, no alternating rows, 22 pt rows, 10 pt intercell spacing, single selection,
   header shown. Double-click a row = Join (8.8); a double-click on the header is ignored.
   Columns in 8.3.
4. **Players header**: title (12 pt semibold, truncating) "Players" / "Players on <name>",
   note (11 pt secondary), a borderless refresh button (`arrow.clockwise` 10 pt; tooltip "Ask
   the server again"; accessibility "Ask again who's playing"; hidden unless an answer — or a
   failed one — is shown), flexible space, small button "Map Info", small button
   "3D Map Preview".
5. **Players table** (boxed, scrolling, **112 pt** tall): full-width style, 18 pt rows, no
   selection highlight. Columns: `"Name"` 300 pt (stretches), `"Score"` 50, `"Ping"` 44,
   untitled 34 (the "bot" tag). 8.6.
6. **Link row** (hidden unless another app handles `stvef://` links, 8.9): label (11 pt
   secondary) and a small button "Open Join Links with Elite Force".
7. **Footer**: address field (260 pt, placeholder "Or an address, like 203.0.113.5:27960"),
   "Connect" (disabled until the field has non-blank text), flexible space, "Cancel" (Escape,
   ≥ 80 pt), "Join" (Return/default, ≥ 80 pt, disabled until a server is selected).

### 8.3 Server table columns (`S:5159-5196`, `S:5749-5867`)

| Id | Header | Width | Sort key (direction on first click) | Cell |
|---|---|---|---|---|
| `fav` | star icon (`star` 10 pt), tooltip "Favorites" | 16, not resizable | `favorite` (descending) | borderless button: `star.fill` yellow when a favourite, else `star` tertiary; tooltip "Remove from favorites" / "Add to favorites"; click toggles (8.5) |
| `name` | "Server" | 162, **the only column that takes up extra width** | `name` (ascending, locale case-insensitive) | the coloured hostname (7.9) at 13 pt, plus `" (this Mac)"` (normal colour) when hosted here via Loopback Host Fix; tooltip: the address (or `"<address>, hosted on this Mac (Loopback Host Fix)"`), plus `"\nListed on <tabs joined by ", ">"` in Combined |
| `lock` | lock icon (`lock.fill` 10 pt), tooltip "Needs a password" | 14 | `needPass` (descending) | `lock.fill` (secondary) shown only when it needs a password; tooltip "Needs a password" |
| `map` | "Map" | 110 | `map` (ascending, case-insensitive) | the map name; Expansion Pack map not installed → purple, tooltip "<map> is an Expansion Pack map, and the Expansion Pack isn't in your game files. Servers can't send it: add the Expansion Pack's files with Add Game Files… in the launcher to join."; other missing map → amber, tooltip "<map> isn't in your game files. It downloads when you join, if the server allows downloads." |
| `players` | "Players", tooltip "Players on the server, bots included, and its maximum" | 74, right-aligned | `players` (descending) | `"<players>/<max>"` (or `"<players>"` when max ≤ 0); secondary when 0; tooltip when humans ≥ 0 and players > humans: `"<h> person|people, <b> bot|bots"` |
| `humans` | "Humans", tooltip "People playing, without bots (? if the server doesn't say)" | 68, right | `knownHumans` (descending; `?` counts as 0) | `"<humans>"` or `"?"`; secondary when ≤ 0 or `?`; `?` tooltip "This server doesn't say how many of its players are bots" |
| `type` | "Game" | 120 | `gametype` (ascending) | 2 → "Single Player"; > 4 → "Type <n>"; else `gametypeName` (0 FFA, 1 Tournament, 3 Team Holomatch, 4 Capture the Flag) |
| `cmod` | "cMod", tooltip "Servers running cMod (by Noah Metzger), with its version" | 40, centred | `cmodRank` (descending) | see below |
| `ping` | "Ping" | 40, right | `ping` (ascending) | `"<ping>"`; green < 80, normal < 160, orange ≥ 160 |

Text cells are 13 pt, truncating tail. Header widths are user-resizable except `fav`.

**cMod column** (`S:4609-4619`, `S:5846-5852`): `cmodNumber` = the first match of
`\bv([0-9]+\.[0-9]+)` in `cmodVersion` (e.g. "v1.29_GIT_f037859-2023-09-30 linux-x86_64 …" →
"1.29", "cMod v2.0 Alpha …" → "2.0"). Cell: no cMod → empty; number → the number; cMod with no
number → `"✓"`. Tooltip when cMod: `"cMod <version>"` (a leading `"cMod "` in the version is
not doubled) or "cMod (the server doesn't say which version)". `cmodRank` for sorting: 0 if not
cMod, the number as a double, or 0.01 when cMod without a number. The column fills in as the
batch's getstatus answers arrive (7.4) or when a server is selected (8.6).

**Sorting** (`S:5188-5192`, `S:5471`, `S:5483-5487`): the default sort descriptors are, in
order: `favorite` descending, `knownHumans` descending, `players` descending, `ping`
ascending. Clicking a column header makes that column the primary key (toggling direction on
repeated clicks) and keeps the previous keys as tie-breakers (standard AppKit behaviour:
the new descriptor is inserted first, any older descriptor with the same key is removed).
Sorting is stable over the filtered list and re-applied on every refresh; the selection is
kept by address across refreshes (`S:5473-5475`). The sort order is **not persisted**.

### 8.4 Fetching a source (`S:5292-5453`)

- Opening the sheet selects **Combined** (clamped to Local when there are no masters,
  `S:5296-5303`, `S:5984`). Selecting a source shows its cached results; it fetches only if the
  source has never been fetched in this sheet. **Refresh** (button or shortcut) re-fetches the
  current source. Results per source live as long as the sheet.
- `fetch(s)` (`S:5331-5442`):
  1. Cancel any running fetch of the same source; start a new one; clear its results.
  2. Status: one master → `"Asking <master> for its servers…"`; Combined →
     `"Asking <n> master servers for their servers…"`; Local → `"Looking on your network…"`.
  3. Background: query every master of the source **in parallel** (7.1). Then, if Loopback
     Host Fix is on and the public address isn't known yet, wait up to 1 s (20 × 50 ms) for it
     (only for internet sources). `own` = the public address when the fix is on.
  4. Merge: for each master that answered (in list order), for each `ip:port`: if `own` is set
     and the entry starts with `"<own>:"`, rewrite it to `"127.0.0.1:<port>"` and mark it
     **this Mac**. Record, per address, the tab labels (`"Internet <n>"`, from the master's
     position in the sheet's master list) of the masters listing it.
  5. Internet sources, on the UI thread (unless cancelled): if **no master answered** → the
     fetch ends; status = one master: `"<master> didn't answer. It may be offline: try another
     source, or change the list in Servers…."`, Combined: `"None of the master servers
     answered. Check your internet connection, or change the list in Servers…."` (note the
     ellipsis followed by a full stop). Otherwise status = one master:
     `"<master> lists <count> server|servers, asking each…"`; Combined:
     `"<answered> of <n> master servers list <count> server|servers, asking each…"`.
  6. Query every listed address (7.4), `broadcast` = Local. As each info arrives: set
     `thisMac`, set `listedOn` (Combined only), append to the source's results, schedule a
     reload. As each status arrives: set `cmodVersion` on the matching result(s), store it in
     `statuses[address]` (the player list uses it), schedule a reload.
  7. Done (UI thread): `n` = results; `servers` = `"<n> server|servers"`; `quiet` =
     `" · <count−answered> listed didn't answer"` when some listed servers didn't answer. Status:
     - one master: count = 0 → `"<master> lists no servers right now."`; else
       `"<master> · <servers><quiet>"`.
     - Combined: count = 0 → `"The master servers list no servers right now."`; else
       `"<servers> from <answered masters> of <n> master servers<quiet>"`.
     - Local: n > 0 → `"Your network · <servers>"`; else `"No servers on your network
       (Holomatch looks on ports 27960 to 27963). Type an address below to join another."`
     - Log `Browser: <status>`.
- **Reload coalescing** (`S:5444-5453`): reloads requested while one is pending are merged;
  the reload runs 100 ms after the first request.
- **show()** (`S:5455-5481`): for the current source, mark favourites; filter (8.5); sort;
  reload; reselect the previously selected address if still shown; set the status text; the
  spinner runs and is visible only while the source's fetch is running; Join enabled iff a
  row is selected; refresh the players panel.

### 8.5 Favourites and filters (`S:5502-5527`)

- **Star** click: toggle the row's address in `favorites`; store `BrowserFavorites` = the set
  as an array **sorted** with plain string compare; refresh. Favourites are keyed by the
  address string as shown (after the loopback rewrite, so a server hosted here is favourited
  as `127.0.0.1:<port>`).
- **Only favorites**: keep only favourites. **Only servers with people**: keep servers with
  `humans > 0`, or with `humans` unknown (−1) and `players > 0`. Both can be on. Each toggle
  stores `BrowserOnlyFavorites` / `BrowserOnlyPeople` (booleans) and refreshes.
- **Globe**: open `https://efservers.com/` in the default browser.

### 8.6 Who's playing (`S:5529-5606`, `S:5716-5746`)

On every selection change and refresh (`showPlayers`):

- Map Info button: hidden when there is no selection, the map is empty, or the map is in
  `builtinMaps`; tooltip "See <map> on efservers.com: screenshots and its files".
  Then `show3D` (8.7).
- No selection: title "Players", note "Select a server to see who's playing.", empty table,
  small refresh hidden.
- Selection without a stored status: title "Players on <name>", note "Asking the server…",
  empty table; unless a query for this address is already out, query it (7.5). On the answer:
  clear `asking` if it was this address; if answered, store it, set the server's
  `cmodVersion`, reload the server table, and if the server is still selected show it; if no
  answer and still selected: note "The server didn't answer." and show the small refresh.
- Selection with a stored status: sort the players **people first** (ping > 0 before ping 0),
  then by **score descending** (ties keep their order). `pinged` = players with ping > 0;
  `humans` = the status's `g_humanplayers` if ≥ 0, else the info's; **tagBots** = humans
  unknown (< 0) or `pinged == humans`. Note: no players → "No one is playing."; else
  `"as of <time, short style>"`. The small refresh is shown.
- **Small refresh**: forget the stored status for the selected address, then `showPlayers`
  (so it asks again).
- Player cells (11 pt, truncating; non-name columns right-aligned): Name = coloured name
  (7.9); Score; Ping (tertiary when 0); last column `"bot"` (secondary) when tagBots and
  ping = 0, else empty.

### 8.7 Map Info and 3D Map Preview (`S:4908-4996`, `S:5608-5675`)

- **show3D**: hide "3D Map Preview"; if Map Info is hidden, stop. If the map's 3D lookup is
  cached: show the button when a URL was found (tooltip "Look around <map> in 3D, on
  efservers.com"). Else, unless this map is already being looked up, fetch
  `getPK3info.php?pk3=<map>&preview` (7.8); on the result (UI thread) cache it (a network error
  is not cached), log `Browser: 3D view of <map>: <url | none | error>`, and re-run show3D if
  that map's server is still selected.
- **Map Info** button: opens the preview sheet with
  `https://efservers.com/pk3.php?preview&popup=1&map=<map>`, title `<map>`.
- **3D Map Preview** button: opens the preview sheet with the cached 3D URL, title
  `"<map> in 3D"`.
- **Preview sheet** (on top of the browser sheet): a 720×520 web view, then a row (16 pt side
  insets): label `"<title> on efservers.com"` (11 pt secondary), flexible space,
  "Open in Browser", "Done" (Return). 12 pt between, 14 pt bottom margin. The web view has
  initial focus (keys move around in 3D). "Open in Browser" opens the URL externally and
  closes; "Done" closes and stops loading. A user script injected at document end:
  `if( navigator.xr && !navigator.xr.isSessionSupported ) navigator.xr.isSessionSupported = function() { return Promise.resolve( false ); };`
  (WebKit's partial WebXR made the 3D page hang at "Loading..."). Logs
  `Browser: map preview <url>`, `Browser: map preview loaded (<page title>)`,
  `Browser: map preview failed: <error>`.
  Windows: WebView2; Linux/Qt: QtWebEngine (or, if a web engine is too heavy, open both pages
  in the default browser — note as a deliberate parity gap if chosen).

### 8.8 Joining (`S:5869-5975`)

- **Join** button, Return, or double-click a row → `joinRow(selected row)`:
  - If the server's map state is **Expansion Pack not installed** (`mapState`: empty map or
    in `haveMaps` → have; in the Expansion list → expansion; else missing), show a warning
    sheet: message "This server's map is from the Expansion Pack.", informative
    `“<name>” is running <map>, which isn't in your game files. Servers can't send the
    Expansion Pack's maps, so joining will fail. Add the Expansion Pack's files with Add Game
    Files… in the launcher, or pick another server.`, buttons "Cancel" (first, default) and
    "Join Anyway". Join Anyway → continue.
  - If the server needs a password: sheet with message `“<name>” needs a password.`,
    informative "Ask the person running the server for it.", a 240×22 **masked** field
    (initial focus), buttons "Join" (default) and "Cancel". Join → password = field text with
    every `"` removed; empty → none.
  - Finish with the address (and password).
- **Connect** (or Return in the address field — on macOS the field's default action is the
  sheet's default button Join, so in practice Connect is clicked): trimmed text; must be
  non-empty and contain only `A–Z a–z 0–9 . - : [ ] _`; otherwise beep and stay. Finish with
  that address and **no password** (the engine resolves host names and prompts for nothing;
  a passworded server will refuse, as in the game).
- **Finish** (`S:5877-5888`): cancel every running fetch; log `Browser: join <address>` or
  `Browser: cancelled`; call the launcher's callback **only with an address**; close the sheet.
- **Cancel** (Escape): finish without an address.

### 8.9 `stvef://` link handler row (`S:5677-5709`)

On build, find the app registered for `stvef://connect/127.0.0.1:27960`. If there is one and it
is not this app (paths compared after resolving symlinks), show the link row with
`"Join links on websites (stvef://) open in <that app's name without extension>."` and the
button "Open Join Links with Elite Force", which makes this app the default handler for the
`stvef` scheme and re-checks. Log `Browser: stvef:// links open in <path | no app>` and
`Browser: open stvef:// links with this app: <done | error>`. Windows: the handler is
`HKCU\Software\Classes\stvef\shell\open\command`; Linux: `xdg-mime query default
x-scheme-handler/stvef` / `xdg-mime default <app>.desktop x-scheme-handler/stvef`.

### 8.10 Snapshot hook (`S:5991-6036`)

`VSP_SnapshotServerBrowser(path, tab, join, dataFolder, done)`: select `local`, `combined` or
Internet `<n>`; wait while busy (fetches, a status query or a 3D lookup); log each row as
`[test] row: <address> | <name> | <map> | cMod <text (version) | ->`; optionally select a row
(`VOYAGERSP_TEST_BROWSER_SELECT`) and wait; render; optionally hold it on screen
(`…_HOLD`), open the preview (`…_PREVIEW`, `…_PREVIEW_3D`), and join row `join`
(`…_EXPANSION=anyway|cancel` answers the Expansion warning).

---

## 9. Hosting: the server window (`VSPServerWindowController`, `S:2033-3839`)

Runs in the `--host-server` instance (1.4). `VSP_RunServerWindow` (`S:3828-3839`): create the
controller, **register** this instance as running (9.13) with the configured name and port,
run the window modally until it ends, **unregister**, log `Server: window closed`.

### 9.1 Initialisation (`S:2229-2254`)

- Keep `settings`, `basepath` (the game folder to pass as `fs_basepath`, may be none),
  `dataFolder`, `clientArgs`; load the map catalog.
- Test hook `VOYAGERSP_TEST_SERVER_INTERNET`: force `internet = openPort = YES` (in memory).
- `port` = settings.port (updated later to the port actually bound); `lanAddress` =
  `VSP_LANAddress()` (once).
- Build the window; state **Starting**; show the info.

### 9.2 Window layout (`S:2256-2485`)

- Content 720 × **720** pt (**840** pt for an internet server); titled, closable,
  minimisable, resizable; full-size content with a transparent, hidden title bar; minimum
  content size 640 × 600. Window title (for the window list/taskbar):
  `"<hostname> · Elite Force Server"`. Centred on open. Initial focus: the command field.
- Root column, insets 34 top / 28 sides / 22 bottom, 16 pt spacing (20 after the header,
  12 after the Server group, 10 after the players/console area, 20 after the command row);
  every child spans the full width.

1. **Header**: the app icon (52 × 52), then a column (3 pt) of the **title** (hostname, 22 pt
   bold, truncating) and a **state row**: an 8 pt `circle.fill` dot (tinted per state) and the
   state text (13 pt).
2. **"Server" group** (a captioned group box) with a form grid, label column 80 pt, 6 pt
   rows:

   | Row | Label | Content | Visible |
   |---|---|---|---|
   | 0 | "Address:" | address (15 pt semibold monospaced digits, selectable) + small "Copy" | always |
   | 1 | — | address hint (11 pt, wraps at 480, truncating) | always |
   | 2 | "Internet:" | status icon + internet value (selectable) + small "Copy" | internet only |
   | 3 | — | internet hint (wrapping) | internet only |
   | 4 | "Listed:" | one row per master (9.11) | internet only |
   | 5 | — | list status (11 pt secondary) + small "Check Again"; below it, list hint (wrapping, orange) | internet only |
   | 6 | "Map:" | map value (13 pt) | always |
   | 7 | "Game:" | game value | always |
   | 8 | "Players:" | players value | always |

3. **Controls**, two rows of three equal-width buttons (13 pt leading icons), 8 pt apart:
   - "Next Map" (`forward.end.fill`), tooltip "Go to the next map in the rotation"
   - "Restart Map" (`arrow.counterclockwise`), tooltip "Restart the current map; scores start over"
   - "Change Map" pull-down (`map` icon on its title item), tooltip "Switch to another map now"
   - "Add Bot" (`person.badge.plus`), tooltip "Add one bot at the skill set in Config…"
   - "Remove Bots" (`person.2.slash`), tooltip "Kick all bots (and stop filling the game with bots)"
   - "Message…" (`text.bubble`), tooltip "Send a message to everyone on the server"
4. **Players and console**, side by side, 14 pt apart, both at least 160 pt tall (the console
   gives way first when the window shrinks; the area takes the extra height):
   - Left, 230 pt wide: header row with "PLAYERS" (11 pt semibold secondary), flexible space,
     small "Kick" button (tooltip "Kick the selected player or bot"); below, the boxed players
     table: columns "Player" (104, takes extra width), "Score" (42, right-aligned header),
     "Ping" (38, right-aligned header); plain style, 20 pt rows, cells 12 pt monospaced digits
     (name left, others right); the name's tooltip is the client's address, or "Bot". Context
     menu on rows: "Kick" (acts on the clicked row).
   - Right: "CONSOLE" caption and the boxed log: read-only plain text, 11 pt monospaced,
     6 pt inset, wraps to the width, scrolls.
5. **Command row**: a text field (stretches), placeholder
   "Console command, such as say Hello, map hm_borg1 or kick <name>"; Return sends; "Send".
6. **Footer**: large "Play on This Mac" (leading `play.fill`; tooltip "Start Elite Force in
   Holomatch and join this server"), flexible space, large "Stop Server" (≥ 110 pt).

### 9.3 States (`S:2171`, `S:2489-2515`)

| State | Dot | Text |
|---|---|---|
| Starting | orange | "Starting…" |
| Running | green | internet: "Running · on your network and the internet"; else "Running · on your local network" |
| Idle | orange | "Running, but no map is loaded" |
| Stopping | orange | "Stopping…" |
| Stopped | tertiary | "Stopped" |
| Failed | red | "Stopped" (state text red) |

With a detail the text is `"<text> · <detail>"`. Text colour is secondary except Failed (red).

Enabling on every state change:

- "Play on This Mac": Running only.
- Command field and "Send": Starting, Running or Idle.
- Stop button title "Close" when Stopped or Failed, else "Stop Server"; disabled while
  Stopping.
- Server controls (Next/Restart/Change Map/Add Bot/Remove Bots/Message…): enabled only when a
  child exists **and** the state is Running. "Kick": same, plus a valid selected player row.

### 9.4 Info texts (`S:2517-2564`)

- Address: `"<lanAddress or 127.0.0.1>:<port>"` (the bound port once known).
- Address hint (tooltip = the same text):
  - no LAN address: "This Mac isn't on a network: only Play on This Mac can join."
  - port outside 27960–27963 (orange): "Port <port> is outside 27960–27963, so Holomatch's Join
    menu won't list it on your network: players connect to this address." + (internet:
    " From the internet: your public address, with the port forwarded.")
  - internet: "On your network, players find it in Holomatch's Join menu, or connect to this address."
  - local: "Players on your network find it in Holomatch's Join menu, or connect to this address."
- Map: the current map from the catalog as `"<displayName> (<name>)"`, or just the name when
  the two are equal, or the raw current map name when not in the catalog, or `"—"` when none;
  then `" · <n> maps in rotation"` when there is a current map and the **configured** rotation
  has more than one entry.
- Game: `gametypeName`, plus `" · <modifier names joined by ", ">"` when any.
- Players: while Starting or with no current map → `"Up to <maxPlayers>"`; else
  `"<n> of <maxPlayers>"` + (bots > 0: `" · <b> bot|bots"`), counted from the last status.
- Copy: puts the address string on the clipboard. Internet "Copy": `"<public>:<publicPort>"`
  (only when a public address is known).

### 9.5 The child process (`S:2586-2646`)

`start`:

1. Write `vsp_server.cfg` (5.1). On failure: log the error (red), state Failed ·
   "couldn't start".
2. Arguments (argv, no shell):

   ```
   +set com_holomatch 1 +set dedicated <2 if internet else 1>
   +set net_port <settings.port> [+set fs_basepath <basepath>] +exec vsp_server.cfg
   ```

3. Executable: the app's own executable (macOS `Contents/MacOS/<exe>`). Environment: this
   process's environment minus every `VOYAGERSP_TEST_*` variable (`S:2079-2087`).
4. **stdout and stderr go to one pipe**; stdin is a pipe kept open for commands.
5. Ignore `SIGPIPE` in the host (a server that quits must not kill the host). Windows: writes
   to a broken pipe just fail; handle the error.
6. Launch. Failure → log "Couldn't start the server: <error>" (red), Failed ·
   "couldn't start".
7. Remember the pid for an **atexit** handler that sends SIGTERM to the child if the host
   exits with it still running (`S:2037-2043`). Windows: put the child in a **Job Object**
   with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` (and still try a graceful `quit` first);
   Linux: `prctl(PR_SET_PDEATHSIG, SIGTERM)` in the child plus the atexit handler.
8. Log `Server: starting <exe path> <args joined by " ">`; console line (secondary)
   `Starting the server: <args joined by " ">`.
9. Start the **status poll** timer: every 2 s (9.6).

The child is headless (docs/macos-port-notes.md "Dedicated server"): it reads console
commands from stdin line by line and prints plain text (colour codes stripped) to stdout, line
buffered; after every map load it prints
`Server ready: map <map> port <bound port> gametype <n> maxclients <n> dedicated <1|2> hostname <name>`
(`server/sv_init.c:640`); `quit` or SIGTERM exit 0; no game data exits 1; a bad
`fs_basepath` exits 3. If `net_port` is taken the engine binds up to 9 ports higher and
reports that one. Windows: the child needs the same headless behaviour (no console window:
create it with `CREATE_NO_WINDOW`, pipes for stdio).

**Reading output** (`S:2619-2625`, `S:2843-2860`): output is read on a background thread and
handed to the UI thread. Bytes are appended to a pending buffer; every complete line (`\n`)
is decoded as UTF-8 (ISO-8859-1 fallback), trailing `\r` removed, and handled (9.7); the
remainder stays pending. End of file stops reading. On child exit the pending remainder is
flushed as a final line.

**Sending** (`S:2822-2831`): if the child is running, write `<command>\n` (UTF-8) to its
stdin; a write error is logged as `Server: couldn't send "<cmd>": <reason>`.

### 9.6 Status polling (`S:2833-2841`)

Every 2 s (also while Starting): if a hidden `status` is outstanding and the last poll was
less than 10 s ago, skip; otherwise mark one hidden `status` outstanding (`autoStatus = 1`),
note the time, send `status`. (So at most one hidden poll in flight, re-sent after 10 s if an
answer got lost.)

### 9.7 Parsing the child's output (`handleLine`, `S:3432-3522`)

Each line is first colour-stripped (`line`; the raw one is kept as `raw`). With the
`VOYAGERSP_TEST_SERVER_START` hook every line is echoed to stdout as `[server] <line>`.

Regexes:

```
rowRE   ^\s*(\d+)\s+(-?\d+)\s+(\d+|CON|ZMB)\s(.*)$        (num, score, ping, rest)
tailRE  ^(.*?)\s+(\S+)\s+(-?\d+)\s*$                      (name, address, rate)
readyRE ^Server ready: map (\S+) port (\d+)
portRE  ^Opening IP socket: [^ ]*:(\d+)
```

The engine's `status` output (`server/sv_ccmds.c:1178-1242`) is:

```
map: <mapname>
cl score ping name            address                                 rate
-- ----- ---- --------------- --------------------------------------- -----
 0    12   48 Munro^7          ^7192.168.1.20:27960                       25000
 1     3    0 Chakotay^7       ^7bot                                      25000
<empty line>
```

(ping is a number, `CON` while connecting or `ZMB` for a zombie; the address is `bot` for
bots; the headless server strips colour codes so the `^7` may be absent). Not running:
`Server is not running.`

Processing, in this order (first match wins):

1. **Inside a status answer** (`inStatus`): the line is shown only if this answer is being
   shown (`statusShown`).
   - A blank (whitespace-only) line ends it (**endStatus**).
   - A line matching `rowRE` (on the **raw** line) is a player: `rest` = group 4 with every
     `^7` replaced by a space; `tailRE` on `rest` gives name, address, rate (no match: the
     whole rest is the name, address empty). `bot` = address is `bot`. Row:
     `name` = colour-stripped and trimmed; `score` = group 2; `ping` = `"bot"` for bots,
     `"…"` for `CON`, else group 3 (so `ZMB` shows as `ZMB`); `address`; `num` = group 1
     (the client slot, used by Kick).
   - Other lines (the header and dashes) are ignored.
2. A line starting with `"map: "` begins a status answer: `statusShown` = (a user status is
   pending) or (no hidden one is pending); decrement the user count if > 0, else the hidden
   count if > 0; `currentMap` = the rest, trimmed; clear the rows.
3. `"Server is not running."` while a hidden status is pending and no user status is: hide it,
   decrement the hidden count; if Running: clear the current map and players, state **Idle**,
   refresh.
4. `readyRE` (on the stripped line): `currentMap` = group 1; `port` = group 2; if it differs
   from the configured port, log (orange) "Port <configured> is in use; the server took
   <bound>."; state **Running**; **becameReady**; refresh.
5. `portRE`: `port` = group 1; refresh (the address shows the real port early).

Then: the dedicated console prompt lines `"]"` and `"] "` are never shown. A shown line is
appended to the console in orange if it starts with `"WARNING"`, red if it starts with
`"ERROR"` or `"Error"`, else the normal colour.

**endStatus** (`S:3417-3430`): leave the status; the rows become the player list; if
Starting or Idle → **Running**; **becameReady**; reload the players table; refresh.
(So a status answer with a map also counts as ready.) With the test hook it logs
`[test] status: map <map>, <n> players: <names joined by ", ">`.

Typed `status` (user) is counted (`userStatus++`) so its answer is shown in the console;
hidden polls are not shown.

### 9.8 becameReady (`S:3365-3415`)

Once per window (the first time only):

1. Log `Server: ready on <address string> (map <map>)`.
2. Re-register this instance with the **bound** port (9.13).
3. Internet + openPort → **open the router port** (9.9).
4. Internet → **start the listing check** (9.10).
5. Test hooks: `VOYAGERSP_TEST_SERVER_ACTIONS` (comma list, each 4 s apart: `next`,
   `restart`, `addbot`, `removebots`, `slot<N>`, `map:<name>`, `kick<row>`), and
   `VOYAGERSP_TEST_SERVER_COMMAND` (sent once, shown as `] <cmd>`).

### 9.9 Router port mapping (`S:2862-3031`)

`openRouterPort` (only once at a time): clear any previous problem/public address; log
`Server: asking the router to forward UDP <port>`; request a UDP mapping internal = external =
the bound port, default lease (7.11), callbacks on the UI thread; start a **15 s** timer; show.
If the request can't even be made, treat its error as the result.

On the 15 s timer, if still opening → **failed**: "Your router didn't answer the request to
open the port: it may not support automatic port opening (NAT-PMP / UPnP), or has it turned
off."

Result handling (`natResult`):

| Outcome | Effect |
|---|---|
| unsupported | failed: "Your router doesn't support automatic port opening (NAT-PMP / UPnP), or has it turned off." |
| disabled | failed: "Automatic port opening (NAT-PMP / UPnP) is turned off in your router's settings." |
| double NAT | failed: "Your network has two routers in a row (double NAT), so the outer one still blocks the port." |
| other error *n* | failed: "The router didn't open the port (error <n>)." |
| no error, external address 0 | ignore (no answer yet; the timer gives up) |
| no error, private external address (7.11) | failed: "Your router opened the port, but its own internet address (<ip>) is private: your provider or another router puts it behind a second NAT, which nobody outside can get through." |
| no error, public address | success: stop the timer; `publicAddress` = it; `publicPort` = the external port, or the bound port when 0; log `Server: the router forwards <ip>:<port> to this Mac`; console (green) "Router: port opened, players on the internet connect to <ip>:<port>" |

**failed(why)**: stop opening and the timer; remember `why`; clear the public address; log
`Server: port not opened: <why>`; console (orange) `Router: <why>`; show.

`closeRouterPort` (on child exit): stop the timer, release the mapping (removes it), log
`Server: router port mapping removed` if there was one, clear the public address.

**Internet row** (`showInternet`, internet servers only); `manual` = "Forward UDP port <port>
to this Mac (<lanAddress or "this Mac's address">) in your router's settings, or use Tailscale
(see the README).":

| Condition (first that holds) | Icon | Value | Hint |
|---|---|---|---|
| public address known | `checkmark.circle.fill` green | `"<ip>:<publicPort>"` in 15 pt semibold monospaced digits; Copy shown | "Your router forwards this address to the server while it runs. Friends on the internet connect to it, or find the server in Holomatch's internet list." |
| opening | `arrow.triangle.2.circlepath` secondary | "Opening the port on your router…" | (hidden) |
| failed | `exclamationmark.triangle.fill` orange | "Port not opened" | `"<why> <manual>"` (orange) |
| openPort off | none | "Your public address" | `"Automatic port opening is off. <manual>"` |
| otherwise | none | "Waiting for the server…" while a child exists, else "—" | (hidden) |

Values other than the address are 13 pt regular. The hint is hidden when empty; Copy is
hidden unless the address is known.

### 9.10 Internet listing check (`S:3033-3283`)

A master lists a server only after it reached it from the internet (it answers the heartbeat
with a `getinfo` to the public address), so being listed proves the port is open. The check
asks the masters for their lists, as the game's browser does.

Constants (`S:3038-3040`): first check **8 s** after ready, then every **20 s**, at most **9**
checks (≈ 3 min). With `VOYAGERSP_TEST_LIST_FAST`: 2 s, 2 s, 2 checks.

- **startListCheck**: stop any previous check; reset counters and results; `checking = YES`;
  start an **address-only** public address lookup (7.11) if not already running (its answer
  sets `lookupAddress`); schedule the first check; show.
- **stopListCheck**: cancel the timer, `checking = NO`, release the lookup; show.
- **checkList**: return unless a child exists and checking. `publicIP` = the mapping's address
  if known, else the lookup's. `want` = `"<publicIP>:<publicPort if mapped, else the bound
  port>"`, or none if no public IP. Count the check. In the background, query **every master
  of the current saved list** (`VSP_MasterServers()`, read now) in parallel (7.1): no answer →
  **down**; answered and `want` in its list → **listed**; else **missing** (also when `want` is
  unknown). Then on the UI thread → **listResult**. (Another server behind the same public IP
  is distinguished by the port.)
- **listResult(listed, missing, down)**: return unless a child exists and checking. Log
  `Server: internet list check <n>: listed on [<a, b>], not on [<…>], no answer from [<…>]`.
  For masters newly listed since the previous check, console (green)
  `"Internet: listed on <labels>, so players anywhere can reach it."`. Store the three lists;
  `found` = any listed; `allDown` = none listed and none missing.
  - **Done** when (found and none missing) or the check count reached the maximum: stop
    checking; if not found, console (orange) "Internet: no master server answered, so the
    listing couldn't be checked." (all down) or "Internet: not on the server list. The master
    server couldn't reach this server from the internet."; else if some are missing, console
    (secondary) `"Internet: not listed on <labels> (yet)."`.
  - Otherwise schedule the next check in 20 s.
- **Check Again** (`S:3277-3283`): if a child exists: send `heartbeat` to the child (announce
  again), then startListCheck.

Labels (`S:3222-3238`): `MasterLabel(m)` = `"Internet <slot+1>"` when `m` is among the first
five of the current list, else `m`; `MasterLabelsWithNames` joins `"Internet 2 (efmaster.tjps.eu)"`
style labels (or the bare host when the label is the host) with `", "`.

### 9.11 "Listed:" rows and status (`S:3285-3355`)

One row per master of the **current saved list** (6 pt spacing): icon, name (13 pt, ≥ 74 pt;
omitted when the label is the host itself) and host (12 pt secondary):

| Master state | Icon | Tooltip suffix |
|---|---|---|
| listed | `checkmark.circle.fill` green | "Listed: players find your server under <label> in the Join menu." |
| missing | `xmark.circle.fill` red | "Not listed: this master answers but doesn't list your server (yet). Some masters list new servers late, or only copy another master's list." |
| no answer | `exclamationmark.circle.fill` amber | "No answer: this master didn't respond." |
| not checked | `circle.dotted` tertiary | "Not checked yet" |

Row tooltip: `"<label> (<master>): <suffix>"`. With no masters at all: one orange line
"No master servers (see Servers… in the launcher)".

List status (hidden when empty): checking with no completed check → "Checking the masters…";
checking after ≥ 1 → `"Checking again… (<n> of 9)"`; not checking and never checked →
"Waiting for the server…" while a child exists, else empty.

List hint (orange, hidden when empty): after checking ended with nothing listed and not all
down: `"No master could reach your server, so players outside your network can't either<x>.
Forward UDP <port> to <lanAddress or "this Mac"> in your router's settings, or use Tailscale
(see the README)."`, where `<x>` = `" (your router accepted the port request but doesn't
forward it)"` when the router mapping had succeeded, else empty.

"Check Again" is hidden when there is no child, while checking, or when listed everywhere
that answered.

### 9.12 Server controls (`S:2648-2820`)

Commands from buttons are shown in the console as `] <cmd>` (accent colour) and sent:

- **Next Map**: `vstr nextmap`.
- **Restart Map**: `map_restart`.
- **Change Map** (pull-down; its menu is rebuilt each time it opens, `S:2688-2742`):
  - If the rotation is non-empty: a disabled header "Rotation", then each rotation entry
    (indented): `"<displayName> (<name>)"` (or the raw name if not in the catalog), checked
    when it equals the current map (exact, case-sensitive compare). Choosing slot *i* sends
    `vstr vsp_map<i>` (the rotation continues from there).
  - Other catalog maps not in the rotation that support the game type, sorted by display name:
    a separator (only if the rotation section exists), a disabled header "Other maps", then
    each (indented) `"<displayName> (<name>)"` + `" · Expansion Pack"` for expansion maps,
    checked when current. Choosing one sends `map <name>` (the rotation stays where it was).
- **Add Bot**: pick a random name among the 28 below that no current player has (name compare
  case-insensitive); if all are taken, pick from all; send `addbot <name> <botSkill>`.
  Names: Munro, Janeway, Chakotay, Tuvok, Paris, Torres, Kim, Seven, Doctor, Neelix, Biessman,
  Chang, Chell, Foster, Jurot, Telsia, Gowron, Kahless, Sela, Alexandria, Laird, Oviedo,
  Nelson, Odell, Jaworski, Csatlos, Vorik, BorgQueen.
- **Remove Bots**: send `set bot_minplayers 0` then `kick allbots` (both shown), then console
  (secondary) "Bots won't refill this game; Add Bot still adds them." (Only for this run; the
  saved setting is unchanged.)
- **Kick** (button acts on the selected row; the context menu on the clicked row): needs a
  child and a valid row. For a **person** (not a bot) first ask (app-modal): message
  `"Kick <name>?"`, informative "They're disconnected from the server, but can join again.",
  buttons "Kick" (destructive style) and "Cancel". Then send `clientkick <num>`. Bots are
  kicked without asking.
- **Message…**: app-modal alert, message "Message everyone on the server", informative
  "Shown in the game's chat area as coming from the server.", a 300 × 24 text field
  (placeholder "Message", initial focus), buttons "Send" and "Cancel". On Send: replace `"`
  with `'` and `;` with `,`, trim, cut to 150 characters; if non-empty send `say "<text>"`.
- **Command field / Send** (`S:3636-3653`): trimmed; ignored when empty or no child. Typed
  `quit` (any case) → clear the field and **stop** (9.14). Otherwise console `] <cmd>`
  (accent); a typed `status` is counted so its answer is shown; send; clear the field.
- **Play on This Mac** (`S:3655-3679`): start a **new instance** of the app (not the
  `--host-server` mode) with `clientArgs` + (`+set password <password>` when the server has
  one) + `+connect 127.0.0.1:<bound port>`, the environment minus `VOYAGERSP_TEST_*`,
  activated. Log `Server: Play on This Mac: <args joined by " ">`; console (secondary)
  "Starting Elite Force to join this server…"; on failure console (red) "Couldn't start the
  game: <error>"; on success log `Server: Play on This Mac started pid <pid>`. (The password
  is passed as a separate argv element, so spaces are fine.)

### 9.13 Running-servers registry (`S:3779-3826`)

So the launcher can warn about a second server (1.3):

- Directory: `HomeRoot/RunningServers` (the parent of HomeBase + `RunningServers`).
- `RegisterRunningServer(name, port)`: create the directory; write `<own pid>.plist`, an XML
  property list dictionary `{ name: <string>, port: <integer>, started: <date> }`, atomically.
  Called before the window runs (configured port) and again on ready (bound port; `started` is
  refreshed too).
- `UnregisterRunningServer()`: delete `<own pid>.plist` (after the window closes).
- `VSP_RunningServers()`: for each `*.plist` whose name parses to a pid > 0: if the process no
  longer exists (`kill(pid, 0)` fails with `ESRCH`), delete the file and skip; else read it
  (unreadable → skip) and return `{ pid, name (default ""), port (default 0), started
  (default distant past) }`. Sorted by `started`, oldest first.
- Windows/Linux: use JSON or INI (`{"name": "...", "port": 27960, "started": <unix time>}`)
  named `<pid>.json`; liveness on Windows via `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`
  + `GetExitCodeProcess == STILL_ACTIVE` (pid reuse risk accepted, as on macOS).

### 9.14 Stopping, exit and crash handling (`S:3524-3619`, `S:3707-3775`)

`stopThen(response)`:

1. Remember `response` (it is overwritten by a later call, even while stopping).
2. No child, or it isn't running → end the window's modal loop now with `response`.
3. Already Stopping → nothing more.
4. State **Stopping**; console `] quit` (accent); send `quit`.
5. After **5 s**, if still running: log `Server: no exit after quit, sending SIGTERM`, send
   SIGTERM (`terminate`); after **3 s** more, if the same pid still runs: log
   `Server: still running, sending SIGKILL`, SIGKILL. Windows: `quit` → after 5 s
   `GenerateConsoleCtrlEvent`/`TerminateProcess(…, 1)` (no SIGTERM; treat both escalations as
   TerminateProcess if no graceful signal is available).

Triggers: "Stop Server"/"Close" button and the window's close button (closing is refused and
turned into a stop) → response *Stop*; typed `quit` → *Stop*; **Cmd-Q** (a local key monitor),
the app menu's Quit (via `VSP_BeginModalMenu`), the system's Quit Apple event
(`kAEQuitApplication`, e.g. from the Dock or logout) and **SIGTERM** to the host (SIGTERM
ignored, handled by a dispatch source) → response *Cancel*. Both responses end the instance
(the return value is not used by the caller), so: every path stops the child cleanly first,
then the instance exits. Windows: handle `WM_CLOSE`, `WM_QUERYENDSESSION`/`WM_ENDSESSION` and
`CTRL_CLOSE_EVENT`/`CTRL_SHUTDOWN_EVENT`; Linux/Qt: `closeEvent`, `QGuiApplication::commitDataRequest`,
and SIGTERM/SIGINT via a self-pipe.

**Child exit** (`taskEnded`, UI thread):

1. Flush the pending output as a final line.
2. Log `Server: exited (signal|code <n>)`.
3. Forget the pid; close the router port (9.9); stop the listing check; stop the poll and kill
   timers; clear the current map and players.
4. If Stopping: state **Stopped**, console (secondary) "The server stopped.", end the modal loop
   with the remembered response (the instance then exits).
5. Otherwise (it exited on its own) the window **stays open** with its output: console (red)
   - signal: "The server crashed (signal <n>)."
   - exit 1 before ready: "The server found no Elite Force game files (exit code 1)."
   - exit 3 before ready: "The server couldn't use the game folder (exit code 3)."
   - else: "The server quit (exit code <n>)."

   and state **Failed** with detail: after ready "the server quit unexpectedly"; else exit 1
   "no game files"; exit 3 "the game folder can't be used"; else "the server didn't start, see
   the console below". The footer button becomes "Close".

**Run loop** (`S:3707-3775`): centre and show the window (off screen at (−20000, −20000)
with the start test hook); install the Cmd-Q monitor, test timers
(`VOYAGERSP_TEST_SERVER_SNAPSHOT[_AFTER]` default 15 s, `…_PLAY_AFTER`, `…_STOP_AFTER`), the
modal-menu quit handler, the Quit Apple-event handler and the SIGTERM source; **start the
child**; run modally; then tear down and hide.

### 9.15 Console log (`S:2566-2584`)

Append `line + "\n"` in 11 pt monospaced with the given colour (default: the normal text
colour). If the view was scrolled to the bottom (within 4 pt) before appending, keep it at the
bottom. When the log exceeds **300 000** characters, delete from the start through the end of
the line containing character 100 000.

---

## 10. Loopback Host Fix and the public address (`S:4467-4537`)

Problem: most routers don't hairpin, so a server hosted on this network, listed by the masters
under the network's public address, doesn't answer in the host's own internet lists.

- Setting **`LoopbackHostFix`** (boolean): absent → **on**. `VSP_SetLoopbackFix(on)` removes
  the key when on, stores NO when off.
- **Public address lookup** (`VSP_StartPublicAddressLookup`, called once at launcher start):
  unless known or already in progress, start an **address-only** NAT-PMP request (7.11); on
  the first successful non-zero answer store it, log
  `Public address (from the router): <ip>` and release the request. Errors are silent; it is
  not retried. Test hook `VOYAGERSP_TEST_NAT=<ipv4>` sets it directly (`fail` → no address).
- `VSP_PublicAddress()`: the address or none.
- `VSP_LoopbackArgs()` (UI thread): fix off → none. Otherwise, if no answer yet but a lookup
  is in progress, **pump the UI event loop** for up to **1.5 s** (50 ms steps) waiting for it.
  Then: address → `["+set", "cl_loopbackHostIP", <ip>]`, else none.
- The game client (`client/cl_main.c:2774-2797`, `EF_DESKTOP`) rewrites every master-list
  entry whose IP equals `cl_loopbackHostIP` to `127.0.0.1`, keeping the port, logging
  `Loopback Host Fix: <addr> is this network, listed as 127.0.0.1:<port>`. The engine side is
  already portable.
- The browser applies the same rewrite itself (8.4 step 4) and marks such rows "(this Mac)".
- Limitation (documented): a server on **another** computer behind the same router is rewritten
  to 127.0.0.1 too and becomes unreachable from the list; the checkbox turns the fix off.

---

## 11. Persistence

### 11.1 Settings keys (macOS `NSUserDefaults`)

| Key | Type | Absent means | Written by | Read by |
|---|---|---|---|---|
| `ServerSettings` | dictionary, 26 keys (2.1) | all defaults | Config… Done (`S:1976`), Start Server (`L:1386`) | launcher start, `--host-server` instance (`S:224-251`) |
| `MasterServers` | array of strings (≤ 16, cleaned) | the recommended defaults (6.1); an **empty array** means "no masters" | Servers… Done (`S:4435`), test hook | everything that needs masters, read live (`S:3889`) |
| `LoopbackHostFix` | boolean; only `NO` is ever stored | on | launcher checkbox (`S:4485-4491`) | `S:4479-4483` |
| `BrowserFavorites` | array of strings (`ip:port`), sorted | none | star toggles (`S:5515`) | browser open (`S:5062`) |
| `BrowserOnlyFavorites` | boolean | off | filter checkbox (`S:5522`) | browser open |
| `BrowserOnlyPeople` | boolean | off | filter checkbox (`S:5523`) | browser open |
| `LauncherMode` | integer (0 SP, 1 Holomatch, 2 Host) | SP | Start Server writes 2 (`L:1385`) | main launcher spec |

The two app instances (launcher and `--host-server`) share the store; the server instance
only reads `ServerSettings` (at start) and `MasterServers` (when writing the cfg and on each
listing check). Windows/Linux: the store must be safe for two processes (QSettings with
`sync()`, or the registry; an INI file needs a write lock or atomic replace).

### 11.2 Files

| Path | Format | Written | Purpose |
|---|---|---|---|
| `HomeBase/vsp_server.cfg` | engine cfg, UTF-8 (5.1) | each server start | the server's settings, masters and rotation |
| `HomeBase/vsp_masters.cfg` | engine cfg, UTF-8 (5.2) | each Holomatch client start (and server instance start) | `sv_master1-16` for clients |
| `HomeRoot/RunningServers/<pid>.plist` | XML plist dict `name`/`port`/`started` (9.13) | server instance start and ready; deleted at exit; stale ones pruned by readers | "already running" check |
| (engine) `HomeBase/hmconfig_server.cfg`, `Lilium Voyager SP_server.pid` | engine | by the dedicated server | not touched by the launcher |

All launcher writes are atomic (write to a temporary file, rename).

---

## 12. Threading and timing (what matters for correctness)

UI work happens on the main/UI thread only. Network I/O is blocking and runs on background
threads; results are posted back to the UI thread.

| Activity | Thread | Timing |
|---|---|---|
| Map catalog scan | UI thread (synchronous; cached by stamp) | tens of ms; recomputes the stamp each call |
| Master query (`QueryMaster`) | background; masters of a fetch/check run **in parallel** (`dispatch_apply`) | 2 s receive timeout after the last packet; no retry |
| Batch info/status (`QueryServerInfo`) | one background thread per fetch, one socket | 1.5 s (Local) / 2.5 s (masters) after sending, +1.0 s while getstatus answers are pending; 100 ms poll; 2 ms pause per 32 sends |
| Single status (`QueryServerStatus`) | background | 2 tries × 1 s |
| Browser fetch cancellation | flag checked by the worker loop and before every UI callback | Refresh/close cancels |
| Browser table reload | UI | coalesced, 100 ms after the first request |
| Loopback wait in a browser fetch | background | ≤ 1 s (20 × 50 ms) |
| `VSP_LoopbackArgs` wait | **UI thread pumping events** | ≤ 1.5 s |
| Masters sheet checks | background per master; UI updates by address | on open, Check Now, after edits/restores |
| Child output reading | background reader → UI | line assembly on the UI thread |
| Child exit notification | background → UI | flushes output first |
| Status poll | UI timer | every 2 s; one in flight; resend after 10 s |
| Stop escalation | UI timers | `quit` → 5 s → SIGTERM → 3 s → SIGKILL |
| Router mapping | callbacks on UI thread | give up after 15 s |
| Listing check | UI timers + background queries | first 8 s after ready, then every 20 s, ≤ 9 checks |
| Public address lookup | callbacks on UI thread | started once at launcher start; no retry |
| 3D lookup | HTTP session thread → UI | once per map per sheet; errors retried on next selection |

Notes:

- All server-window timers are added in the "common" run-loop modes so they keep firing while
  menus are open, while a modal alert is up (Kick/Message…) and during the window's own modal
  loop. On Win32/Qt use timers that fire inside modal loops (Qt timers do; Win32 `SetTimer` on
  a window does while a modal dialog pumps messages).
- The server window runs as an **application-modal** loop (`runModalForWindow`); ending it
  from a non-event context needs a dummy event posted to wake the loop (`S:3560-3566`).
- The launcher itself is app-modal too, which is why Map Info is a sheet, not a window
  (`S:4908-4909`).
- Every callback that touches a sheet checks the fetch's cancelled flag (results of an old
  Refresh never mix into a new one), and the player-panel callback checks that its server is
  still selected.
- `VSPServerInfo` objects are created on the worker thread and only mutated on the UI thread
  afterwards (cmodVersion, favorite, thisMac, listedOn); keep that ownership rule.
- The status poll and the user's own `status` share one output stream; the counters
  `autoStatus`/`userStatus` decide which answers are shown (9.7). Answers arrive in order.
- The host ignores SIGPIPE before launching the child; the child is killed by an atexit
  handler if the host exits first.

---

## 13. Icons used

| SF Symbol | Where |
|---|---|
| `plus`, `minus`, `chevron.up`, `chevron.down` | rotation and masters buttons |
| `map`, `questionmark` | levelshot placeholder; Change Map title |
| `circle.fill` | server state dot |
| `forward.end.fill`, `arrow.counterclockwise`, `person.badge.plus`, `person.2.slash`, `text.bubble`, `play.fill` | server window controls |
| `checkmark.circle.fill`, `xmark.circle.fill`, `exclamationmark.circle.fill`, `circle.dotted`, `exclamationmark.triangle.fill`, `arrow.triangle.2.circlepath` | statuses (masters, Listed:, Internet:) |
| `star`, `star.fill`, `lock.fill`, `globe`, `arrow.clockwise` | browser |
| `server.rack`, `person.3.fill` | launcher cards (main spec) |

---

## 14. Test hooks (environment variables)

Not user-visible, but the macOS regression scripts use them; port the ones the Windows/Linux
harness needs. All `VOYAGERSP_TEST_*` variables are stripped from every child/new-instance
environment. "Set" means non-empty and not `"0"` (`S:134-138`).

`VOYAGERSP_TEST_SERVER_CONFIG_SNAPSHOT=<png>`, `…_SERVER_CONFIG_TAB=maps|game|server|cmod`,
`…_SERVER_TICKRATE=20|40`, `…_SERVER_START=1` (host mode without the launcher; window off
screen; echoes `[server] …` and `[test] status: …`), `…_SERVER_PORT=<port>`,
`…_SERVER_SNAPSHOT=<png>`, `…_SERVER_SNAPSHOT_AFTER=<s>` (15), `…_SERVER_STOP_AFTER=<s>`,
`…_SERVER_COMMAND=<cmd>`, `…_SERVER_PLAY_AFTER=<s>`, `…_SERVER_INTERNET=1`,
`…_SERVER_NOMASTER=1` (blank masters in vsp_server.cfg), `…_NAT=<ipv4>|fail`,
`…_MASTERS=a,b,…`, `…_MASTERS_SNAPSHOT=<png>`, `…_MASTERS_ACTIONS=…`, `…_LIST_FAST=1`,
`…_SERVER_ACTIONS=next,restart,slot<N>,map:<name>,addbot,removebots,kick<row>`,
`…_SERVER_RUNNING=show|another|cancel`, `…_BROWSER_SNAPSHOT=<png>`,
`…_BROWSER_TAB=local|combined|<n>`, `…_BROWSER_JOIN=<row>`, `…_BROWSER_EXPANSION=anyway|cancel`,
`…_BROWSER_SELECT=<row>`, `…_BROWSER_PREVIEW=<png>`, `…_BROWSER_PREVIEW_3D=1`,
`…_BROWSER_HOLD=<s>`, `…_LINK_HANDLER=<app>`, `…_BACKGROUND=1`.
(`S:60-86`, `L:35-73`.)

---

## 15. Portable core candidates and platform UI

### 15.1 Already plain C inside the `.m` file

These functions/blocks are C (POSIX + zlib) wrapped in Objective-C only for strings and
collections, and can move almost verbatim:

- `LE16`, `LE32`, and the byte-level parts of `ZipList`/`ZipRead` (`S:337-439`; file I/O via
  `NSFileHandle` → `fopen/fseek/fread`).
- `HexNibble`, `ParseServerList` (`S:3043-3084`; only the set insertion is Objective-C).
- The socket parts of `QueryMaster` (`S:3088-3136`), `QueryServerInfo` (`S:4682-4777`),
  `QueryServerStatus` (`S:4849-4879`) — `getaddrinfo`, `socket`, `sendto`, `recv(from)`,
  `poll`, `SO_RCVTIMEO`, `SO_BROADCAST` (Winsock needs `WSAStartup`, `closesocket`,
  `WSAPoll`, and `DWORD` milliseconds for `SO_RCVTIMEO`).
- `ParseAddress` (`S:4629-4640`; `sin_len` is BSD-only — drop it on Windows/Linux).
- `PrivateIPv4` (`S:2872-2876`), `LANDiscoverable` (`S:120`), `TestEnvSet` (`S:134-138`).
- `VSP_LANAddress` (`S:2047-2074`, `getifaddrs`; Linux as is, Windows needs
  `GetAdaptersAddresses`).
- `KillServerAtExit` (`S:2039-2043`).
- The NAT callbacks are C-shaped but depend on `dns_sd` (not portable; 7.11).
- `ParseInfoResponse`/`ParseStatusResponse`/`DecodeColors`/`Tokens`/`ParseArenas` are simple
  string processing that is easy to rewrite in C.

### 15.2 Proposed shared C library (`vsp_srv`)

Conventions: UTF-8 `char *` strings; caller-owned output buffers or `*_free` functions; no
global state except where noted; all functions thread-safe unless stated; async work uses a
**callback + user pointer**, and callbacks are invoked on the **worker thread** — the platform
layer marshals them to its UI thread (Qt: `QMetaObject::invokeMethod(…, Qt::QueuedConnection)`;
Win32: `PostMessage`). A cancel token is a plain atomic flag owned by the caller.

```c
/* ---- settings (2) ---- */
typedef enum { VSP_GT_FFA = 0, VSP_GT_TOURNAMENT = 1, VSP_GT_TEAM = 3, VSP_GT_CTF = 4 } vsp_gametype;
enum { VSP_MOD_ASSIMILATION, VSP_MOD_SPECIALTIES, VSP_MOD_DISINTEGRATION,
       VSP_MOD_ELIMINATION, VSP_MOD_ACTIONHERO, VSP_MOD_COUNT };

typedef struct {
    char hostname[49], password[33];
    int  gametype;
    int  mods[VSP_MOD_COUNT];          /* booleans, in the fixed order */
    int  fraglimit, capturelimit, timelimit, maxPlayers, bots, botSkill;
    int  friendlyFire, internet, openPort, allowDownload, pure;
    int  port, tickRate;
    int  cmodGame, unlagged, smoothClients, unlaggedPredict;
    int  rotationCount;
    char rotation[64][64];             /* or a dynamic list */
} vsp_server_settings;

void vsp_settings_defaults(vsp_server_settings *s, const char *userFullName);
void vsp_settings_validate(vsp_server_settings *s, const char *userFullName); /* 2.3 step 3-4 */
void vsp_settings_apply_modifier_rules(vsp_server_settings *s, int favoredMod /* -1: none */);
int  vsp_settings_modifier_allowed(const vsp_server_settings *s, int mod);    /* 4.3 enabling */
void vsp_clean_cvar_text(const char *in, size_t maxLen, char *out, size_t outSize);
const char *vsp_gametype_name(int gametype);         /* 2.5 */
const char *vsp_browser_gametype_name(int gametype, char *buf, size_t n); /* adds "Single Player", "Type N" */
const char *vsp_skill_name(int skill);
const char *vsp_modifier_name(int mod);
/* key/value bridge so each platform stores the same 26 keys */
typedef struct { void *user;
    int  (*get_int)(void *user, const char *key, int *out);   /* 0 if absent/wrong type */
    int  (*get_str)(void *user, const char *key, char *out, size_t n);
    int  (*get_strlist)(void *user, const char *key, char (*out)[64], int max);
    void (*set_int)(void *user, const char *key, int v);
    void (*set_str)(void *user, const char *key, const char *v);
    void (*set_strlist)(void *user, const char *key, const char (*v)[64], int n);
    void (*remove)(void *user, const char *key);
} vsp_store;
void vsp_settings_load(vsp_server_settings *s, const vsp_store *st, const char *userFullName);
void vsp_settings_save(const vsp_server_settings *s, const vsp_store *st);

/* ---- pk3 / map catalog (3) ---- */
typedef struct { char name[64]; unsigned method, csize, usize, offset; } vsp_zip_entry;
int  vsp_zip_list(const char *path, int (*want)(const char *lowerName, void *u), void *u,
                  vsp_zip_entry **out, int *count);          /* free with free() */
unsigned char *vsp_zip_read(const char *path, const vsp_zip_entry *e, size_t *len);

typedef struct {
    char name[64], longName[128], types[64], pk3[128];
    int  retail, expansion;
    char shotPath[1024]; int shotInZip; vsp_zip_entry shotEntry;  /* levelshot location */
} vsp_map_info;
typedef struct vsp_map_catalog vsp_map_catalog;               /* opaque, cached by stamp */
const vsp_map_catalog *vsp_map_catalog_get(const char *dataFolder, const char *homeBase);
int  vsp_map_catalog_count(const vsp_map_catalog *c);
const vsp_map_info *vsp_map_catalog_at(const vsp_map_catalog *c, int i);
const vsp_map_info *vsp_map_find(const vsp_map_catalog *c, const char *name);
const char *vsp_map_display_name(const vsp_map_info *m);
int  vsp_map_supports_gametype(const vsp_map_info *m, int gametype);
void vsp_map_types_description(const vsp_map_info *m, char *out, size_t n);
unsigned char *vsp_map_levelshot_bytes(const vsp_map_info *m, size_t *len, int *isTga); /* UI decodes */
int  vsp_is_expansion_map(const char *lowerName);              /* 3.8 list */
void vsp_arena_parse(const char *text, void (*block)(const char *map, const char *longname,
                     const char *type, void *u), void *u);

/* ---- cfg generation (5) ---- */
int  vsp_server_cfg_text(const vsp_server_settings *s, const vsp_map_catalog *c,
                         const char (*masters)[65], int masterCount, int blankMasters,
                         char **outText, char *err, size_t errSize,
                         void (*log)(const char *line, void *u), void *u);
void vsp_masters_cfg_text(const char (*masters)[65], int count, int blank, char **outText);
int  vsp_write_file_atomic(const char *path, const char *text);

/* ---- masters (6) ---- */
#define VSP_MAX_MASTERS 16
extern const char *const vsp_default_masters[5];
extern const char *const vsp_original_masters[2];
void vsp_clean_master(const char *in, char out[65]);
int  vsp_clean_masters(const char *const *in, int n, char out[][65]);   /* returns count */
int  vsp_masters_load(const vsp_store *st, char out[][65]);             /* defaults if absent */
void vsp_masters_save(const vsp_store *st, const char (*list)[65], int n); /* removes key if == defaults */

/* ---- protocol (7) ---- */
typedef struct { unsigned char ip[4]; unsigned short port; } vsp_addr4;
int  vsp_parse_server_list(const unsigned char *buf, size_t n,
                           void (*entry)(vsp_addr4 a, void *u), void *u);     /* 7.1 */
int  vsp_query_master(const char *master, int timeoutMs,
                      void (*entry)(vsp_addr4 a, void *u), void *u);          /* blocking; -1 no answer, else #packets */
typedef struct {
    char address[24], hostname[128], name[128], map[64];
    int  players, maxPlayers, humans /* -1 unknown */, gametype, ping, needPass;
} vsp_server_info;
typedef struct { char name[64]; int score, ping; } vsp_player;
typedef struct {
    int humans; int isCmod; char cmodVersion[128];
    int playerCount; vsp_player players[64]; long long when;
} vsp_server_status;
int  vsp_parse_info_response(const unsigned char *buf, size_t n, const char *from, vsp_server_info *out);
int  vsp_parse_status_response(const unsigned char *buf, size_t n, vsp_server_status *out);
int  vsp_parse_address4(const char *s, int defaultPort, struct sockaddr_in *out);
typedef struct {
    void (*got_info)(const vsp_server_info *i, void *u);
    void (*got_status)(const char *address, const vsp_server_status *st, void *u);
    void (*done)(int answered, void *u);
    void *user;
} vsp_query_callbacks;
void vsp_query_servers(const char *const *addresses, int n, int broadcast,
                       const volatile int *cancel, const vsp_query_callbacks *cb); /* blocking, 7.4 */
int  vsp_query_status(const char *address, vsp_server_status *out);            /* blocking, 7.5 */
void vsp_decode_colors(const char *in, char *out, size_t n, unsigned char *colorIdx /* may be NULL */);
void vsp_strip_colors(const char *in, char *out, size_t n);
int  vsp_cmod_number(const char *cmodVersion, char *out, size_t n);           /* "\bv(\d+\.\d+)" */
double vsp_cmod_rank(int isCmod, const char *cmodVersion);                     /* 0, version, or 0.01 */

/* ---- browser model (8) ---- */
typedef enum { VSP_COL_FAV, VSP_COL_NAME, VSP_COL_LOCK, VSP_COL_MAP, VSP_COL_PLAYERS,
               VSP_COL_HUMANS, VSP_COL_TYPE, VSP_COL_CMOD, VSP_COL_PING } vsp_column;
typedef struct { vsp_column column; int ascending; } vsp_sort_key;   /* 8.3 default: FAV desc, HUMANS desc, PLAYERS desc, PING asc */
typedef struct {
    vsp_server_info info;
    int  favorite, thisMac, isCmod;
    char cmodVersion[128];
    char listedOn[VSP_MAX_MASTERS][16];  int listedOnCount;   /* "Internet N" labels (Combined) */
} vsp_browser_row;
typedef struct vsp_browser vsp_browser;   /* sources, results, merge, loopback rewrite, filters, sort */
vsp_browser *vsp_browser_new(const char (*masters)[65], int n, const vsp_map_catalog *c);
void vsp_browser_fetch(vsp_browser *b, int source, const char *publicIP /* or NULL */,
                       void (*changed)(int source, void *u), void *u);   /* spawns a worker */
void vsp_browser_cancel_all(vsp_browser *b);
int  vsp_browser_rows(vsp_browser *b, int source, int onlyFav, int onlyPeople,
                      const char *const *favorites, int nFav,
                      const vsp_sort_key *keys, int nKeys, const vsp_browser_row **out);
const char *vsp_browser_status_text(vsp_browser *b, int source);   /* exact strings of 8.4 */
int  vsp_map_state(const vsp_map_catalog *c, const char *map);     /* HAVE / MISSING / EXPANSION */
void vsp_sort_players(vsp_player *p, int n);                       /* people first, score desc */
int  vsp_tag_bots(const vsp_player *p, int n, int humans);

/* ---- efservers (7.8) ---- */
void vsp_efservers_info_url(const char *map, char *out, size_t n);
void vsp_efservers_3d_lookup_url(const char *map, char *out, size_t n);
int  vsp_efservers_parse_3d(const char *html, char *url, size_t n);   /* 1 found, 0 none */

/* ---- dedicated server host (9) ---- */
typedef enum { VSP_SRV_STARTING, VSP_SRV_RUNNING, VSP_SRV_IDLE, VSP_SRV_STOPPING,
               VSP_SRV_STOPPED, VSP_SRV_FAILED } vsp_srv_state;
typedef struct { char name[64], score[12], ping[12], address[64], num[8]; int bot; } vsp_srv_player;
typedef struct vsp_srv vsp_srv;          /* console parser + state machine, no I/O of its own */
typedef struct {
    void (*send)(const char *cmd, void *u);                    /* write cmd + "\n" to stdin */
    void (*log)(const char *line, int color /* 0 normal,1 orange,2 red,3 green,4 secondary,5 accent */, void *u);
    void (*state)(vsp_srv_state s, const char *detail, void *u);
    void (*players)(const vsp_srv_player *p, int n, void *u);
    void (*info)(const char *currentMap, int boundPort, void *u);
    void (*ready)(int boundPort, void *u);                     /* once */
    void *user;
} vsp_srv_callbacks;
vsp_srv *vsp_srv_new(const vsp_srv_callbacks *cb, int configuredPort);
void vsp_srv_feed(vsp_srv *s, const char *bytes, size_t n);    /* line assembly + 9.7 */
void vsp_srv_poll_tick(vsp_srv *s, double nowSeconds);         /* every 2 s, 9.6 */
void vsp_srv_user_command(vsp_srv *s, const char *cmd);        /* counts "status" */
void vsp_srv_exited(vsp_srv *s, int signaled, int code);       /* 9.14 messages + state */
const char *vsp_srv_addbot_command(vsp_srv *s, int skill, char *buf, size_t n);
void vsp_srv_say_command(const char *text, char *buf, size_t n);   /* sanitise, 150 chars */
/* process spawning: small portable helper (posix_spawn / CreateProcessW + Job Object) */
typedef struct vsp_proc vsp_proc;
vsp_proc *vsp_proc_spawn(const char *exe, const char *const *argv, const char *const *envp,
                         void (*out)(const char *b, size_t n, void *u),
                         void (*exited)(int signaled, int code, void *u), void *u);
int  vsp_proc_write(vsp_proc *p, const char *b, size_t n);
void vsp_proc_terminate(vsp_proc *p);   /* SIGTERM / TerminateProcess */
void vsp_proc_kill(vsp_proc *p);        /* SIGKILL */
int  vsp_proc_pid(vsp_proc *p);

/* ---- NAT-PMP / PCP / UPnP (7.11) and listing check (9.10) ---- */
typedef enum { VSP_NAT_OK, VSP_NAT_UNSUPPORTED, VSP_NAT_DISABLED, VSP_NAT_DOUBLE,
               VSP_NAT_PRIVATE_EXTERNAL, VSP_NAT_TIMEOUT, VSP_NAT_OTHER } vsp_nat_result;
typedef struct vsp_nat vsp_nat;
vsp_nat *vsp_nat_public_address(void (*cb)(vsp_nat_result r, const char *ip, int err, void *u), void *u);
vsp_nat *vsp_nat_map_udp(int port, void (*cb)(vsp_nat_result r, const char *ip, int extPort,
                         int err, void *u), void *u);          /* renews until released */
void vsp_nat_release(vsp_nat *n);                              /* deletes the mapping */
int  vsp_ipv4_private(unsigned hostOrderAddr);
int  vsp_lan_address(char *out, size_t n);                     /* platform #ifdefs inside */
typedef struct { int listed[VSP_MAX_MASTERS], missing[VSP_MAX_MASTERS], down[VSP_MAX_MASTERS]; } vsp_list_result;
void vsp_list_check(const char (*masters)[65], int n, const char *want /* ip:port or NULL */,
                    vsp_list_result *out);                     /* blocking, parallel */

/* ---- running-servers registry (9.13) ---- */
struct vsp_running { int pid; char name[64]; int port; long long started; };
int  vsp_running_register(const char *homeRoot, const char *name, int port);
void vsp_running_unregister(const char *homeRoot);
int  vsp_running_list(const char *homeRoot, struct vsp_running *out, int max); /* prunes stale */
```

The text-producing logic (all hint/status strings that depend on state: 4.3 hints, 4.4
network hint, 8.4 status lines, 9.4 info texts, 9.9 internet row, 9.11 list status/hint, 9.14
messages) should also live in the core as `const char *vsp_text_…(state…)` helpers, so both
UIs show identical strings and future text changes happen once. "Mac" → platform noun can be a
core parameter.

### 15.3 Platform UI only

- All windows/dialogs and their layout: launcher Host summary, Server Settings (four pages,
  number fields with steppers, map cells with thumbnails, drag and drop into/within the
  rotation), Master Servers (live drag reorder, inline edit on double-click), Holomatch Servers
  (segmented sources, sortable table with star/lock cells, coloured names, spinner, players
  panel), preview sheet (web view), server window (header, info grid, controls, players table,
  console with colour and auto-scroll, command field), alerts (already running, Kick, Message…,
  password, Expansion Pack warning).
- Image decoding/scaling of levelshots (bytes come from the core).
- Clipboard (Copy buttons), opening URLs, activating another instance's window, registering the
  `stvef` URL handler.
- Settings store adapter (`vsp_store`), HomeRoot/HomeBase paths, the bundled cmod folder path,
  the user's full name, the app's own executable path.
- Starting the `--host-server` instance and the Play on This Mac instance (detached, with the
  filtered environment), Job Objects / death signals.
- The modal-loop and quit plumbing (Cmd-Q, app menu Quit, session end, SIGTERM).
- HTTP for the 3D lookup (WinHTTP / `QNetworkAccessManager`; or libcurl in the core).
- Gateway discovery for NAT-PMP if not in the core; the Windows firewall rule.

---

## 16. Quirks to keep (or fix deliberately)

Listed so implementers don't "fix" behaviour by accident; each is either intended or a known
limitation of the macOS build.

1. Rotation duplicates are allowed; missing rotation maps are skipped at start, not removed
   from settings.
2. `CleanCvarText` drops non-ASCII characters (accented names lose those letters).
3. The password field in Config… is plain text (not masked); the Join password prompt is masked.
4. `QueryMaster` sends once and never retries; a lost request means "no answer" for that
   master. Its 2 s timeout restarts after each packet.
5. Hex vs binary master-list encoding is decided from the first entry only.
6. `getinfo` uses the fixed challenge `browse`; responses are matched by source address only.
7. Browser IPv4 only (IPv6 masters work; IPv6 servers don't appear).
8. Join with a typed address never asks for a password.
9. Favourites are keyed by `ip:port` as shown, so a server's favourite differs between the
   loopback-rewritten (`127.0.0.1:port`) and normal address.
10. The map column compares names case-insensitively; Change Map's checkmark compares exactly.
11. `stopThen` overwrites the remembered response even while already stopping.
12. The public address lookup at launcher start is never retried; `VSP_LoopbackArgs` waits up
    to 1.5 s on the UI thread for it.
13. The listing check looks for `<public ip>:<port>`; with no known public IP every answering
    master reports "missing" until the check gives up.
14. Loopback Host Fix rewrites every server behind the same public IP to 127.0.0.1 (a second
    computer's server on the same network becomes unreachable from the list).
15. The "already running" alert names the **newest** registered server.
16. The `Change Map` "Other maps" section lists only maps that support the game type; the
    rotation section lists everything in the rotation.
17. The Loopback Host Fix checkbox and the browser's favourites and filters are saved the
    moment they change; Config… and Servers… changes are saved only on Done.
