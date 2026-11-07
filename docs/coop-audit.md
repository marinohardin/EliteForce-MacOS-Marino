# Co-op "who is the player" audit (M0 plan)

Status: plan only. No code has changed. Follows `docs/coop-proposal.md` (its Decisions are approved).
Branch `1_4_0/coop`, which is never merged anywhere.

## Summary

- **The real number of sites is about 280, not about 500.** The proposal's "about 371 `player`
  references" are mostly comments, strings (`sound/player/...`) and a local variable that is
  really an NPC (`NPC_spawn.cpp:5723`). With the patterns below there are 261 grep hits, plus 21
  sites the patterns miss (listed by hand). Total: 282.
- **Counts by group:** story player 97, any player 79, nearest player 15, local client 24,
  leave as is 45, leave + hook 11, dead code 11. Nothing is left unclassified.
- **Only about 140 sites need an edit** (any 79, nearest 15, local 24, hooks about 20). The 97
  story-player sites need **no** edit: P1 stays entity 0 forever, so `COOP_StoryPlayer()` would
  always be `&g_entities[0]`. Converting them would only make the code easier to read, and every
  extra edit is extra risk. This plan leaves them alone (that changes the proposal).
- **Ten batches**, each one file group, each a mechanical swap whose co-op-off form is the same
  expression. The order: scaffolding (B0), P2 puppet core with save exclusion (B1), then
  weapons, combat, movement, triggers/items, AI targeting, AI perception, reactions, gameplay
  hooks, cgame local client.
- **Main risks, which make the design harder than the proposal assumes:**
  1. **Saves:** `gentity_t`/`gclient_t` are written as whole structs (`sizeof`), so no new field
     may be added to them, ever, or `.sav` stops loading in plain SP. P2's identity has to live
     in the co-op module (its entity number), not in an entity flag field.
  2. **Saves again:** a client without an NPC struct is saved as an index into `level.clients`
     (`g_savegame.cpp:348`). P2 would save as a garbage index. Leaving P2 out is mandatory, and
     other entities' pointers to P2 (`enemy`, `owner`, `activator`, leader/follower) must be
     cleared or pointed at P1 first.
  3. **Respawn:** a dead client without an NPC struct takes the player respawn path, which runs
     `load *respawn` (`g_active.cpp:1168`, `g_client.cpp:307`). A dead P2 pressing fire would
     reload the game.
  4. **Build:** `efgame/CMakeLists.txt` globs `src/game/*.cpp` (and is the same file the Android
     build uses), so a `coop_*.cpp` placed in `game/` would compile everywhere. The co-op sources
     go in a new `efgame/src/coop/` folder that is added only with `EF_COOP`.
  5. **Shared movement code:** `bg_pmove.cpp` runs in both the game and the cgame prediction
     (where `pm->gent` is NULL), so its helpers must work on `ps->clientNum`.
  6. **Third-person cvar in game code:** `g_weapon.cpp` reads the host's `cg_thirdPerson` for
     whoever is shooting.
- **M0 needs no bridge (`sp_bridge.cpp`) change.** P2 can be spawned and driven from game code;
  the bridge's entity-0 sites only matter for the guest (M1).

## How the sites were found

Folders: `efgame/src/game`, `efgame/src/cgame`, `efgame/src/icarus`, `EFAndroid-SP/app/jni/sp/`.
Patterns (grep -E):

| Pattern | Hits |
|---|---|
| `g_entities\[ *0 *\]` | 115 |
| `cg_entities\[ *0 *\]` | 4 |
| `(number\|entityNum\|clientNum\|ownerNum\|otherEntityNum2?\|s\.number) *(==\|!=\|>\|<=) *0`, `!x->s.number` | 85 |
| `level\.clients\[ *0` | 44 |
| `MAX_CLIENTS` | 16 |
| `(entNum\|entID\|clientNum\|ps->clientNum) *(==\|!=) *0` | 11 |
| `\bplayer\b` in code, comments and strings removed | 181, of which about 10 are real sites (already in the lines above) |

After removing duplicates: 261 lines. ICARUS itself has no entity-0 code: it finds the player by
the name `Munro` (set in `g_client.cpp:829`). Added by hand (21 sites): `i == 0` in `G_RunFrame`,
the respawn path, the save code's client pointer, NPCONLY checks, the cgame entity binding, the
bridge's entity-0 sites, and the hook targets.

`cg.snap->ps` (about 120 uses in the cgame) is already "me": the snapshot is built from entity 0
on the host and will be built from P2 on the guest. Those uses stay as they are.

The 143 `!ent->NPC` / `ent->NPC == NULL` checks were also scanned, because P2 is the first client
that is neither entity 0 nor an NPC. Almost all of them are "is this an NPC" guards, which
correctly exclude P2. The three that mean "this is the player" are listed in the tables
(`g_active.cpp:1168`, `g_savegame.cpp:348`, the NPCONLY triggers).

## Helpers

All in `efgame/src/game/coop_public.h` (header only; included by every converted file). With
`EF_COOP` OFF they are macros that expand to the exact old expression. With `EF_COOP` ON and
`sp_coop 0`, `COOP_Active()` is false and every helper returns the old value.

| Helper | Co-op off (exact old expression) | Co-op on |
|---|---|---|
| `COOP_Active()` | `0` | latched `sp_coop` read at InitGame, and P2 exists |
| `COOP_IsP2(e)` | `0` | `(e)->s.number == coop_p2Num` (number kept in the co-op module, not in the entity) |
| `COOP_IsPlayer(e)` | `((e)->s.number == 0)` | `s.number == 0 \|\| COOP_IsP2(e)` |
| `COOP_IsPlayerNum(n)` | `((n) == 0)` | `n == 0 \|\| n == coop_p2Num` (for `ps->clientNum`, works in the cgame prediction) |
| `COOP_StoryPlayer()` | `(&g_entities[0])` | same: P1 is always entity 0 |
| `COOP_StoryActivator(e)` | `(e)` | P1 if `e` is P2, else `e` |
| `COOP_NearestPlayer(org)` | `(&g_entities[0])` | the closer of P1 and P2 that is alive and in use |
| `COOP_AnyPlayerInPVS(org)` | `gi.inPVS(g_entities[0].currentOrigin, org)` | either player |
| `COOP_AnyPlayerSees(ent, dist2, hfov, vfov)` | the same three checks on entity 0, in the same order (distance, `InFOV`, `NPC_ClearLOS`) | either player |
| `COOP_ThirdPerson(e)` | `cg_thirdPerson.integer` | P1: same; P2: P2's userinfo setting |
| `COOP_ViewClientNum()` | `0` | number of the client whose view this machine draws |
| `CG_LocalClientNum()` | `0` | 0 on the host, P2's number on the guest |
| `CG_LocalGEnt()` | `(&g_entities[0])` | `&g_entities[CG_LocalClientNum()]` |

Rules for the helpers, so co-op off stays identical:
- No added NULL, `inuse` or health checks in the off form. `COOP_NearestPlayer` returns
  `&g_entities[0]` unconditionally when off, as the old code did.
- Each macro uses its argument once (no double evaluation).
- No new fields in `gentity_t`, `gclient_t`, `playerState_t`, `gNPC_t` or `level_locals_t`:
  the save code writes them by `sizeof` (`g_savegame.cpp` `'GENT'`/`'GCLI'` chunks).

## Sites that are not exactly a drop-in swap (special care)

Off-equivalence holds for all of these, but the code has to change shape, not just a token:

| Site | Why |
|---|---|
| `g_active.cpp:817` | One `if` does two jobs: freeze the player in a camera (any player) and run the cinematic skip (P1 only). Split it. |
| `g_active.cpp:1168`, `g_client.cpp:307` | Dead client without an NPC struct = `load *respawn`. Needs a P2 guard before the respawn branch. |
| `g_savegame.cpp:348, 650/659` | P2 cannot be saved at all; references to it must be scrubbed first. |
| `g_client.cpp:835` | P2 needs its own die function. A new `dieF_` enum value touches the save function tables; append it last, or branch inside `player_die`. |
| `g_combat.cpp:1102` | Pointer compares (`attacker != &g_entities[0]`), not numbers. Same result off. |
| `g_items.cpp:232`, `g_misc.cpp:1311` | `!other->s.number == 0` parses as `(!n) == 0`, i.e. `n != 0`. Keep that meaning: `!COOP_IsPlayer(other)`. |
| `NPC_utils.cpp:1066-1077`, `AI_Utils.cpp:211-218` | Several lines use "the player" together; convert each block as one unit with one local `p`. |
| `NPC.cpp:66-73`, `NPC_spawn.cpp:1013-1017` | "No player can see me" checks become a loop over players. Keep the order of checks for the off form. |
| `g_trigger.cpp:1184` | trigger_visible fires once and frees itself: the first player to look wins. |
| `g_weapon.cpp:220, 337, 1813` | Reads the host cgame's `cg_thirdPerson` for the shooter. |
| `bg_pmove.cpp:1853` | After the player check, the `\|\|` chain calls `Q_stricmp(..., ent->NPC_type)`. P2 must have a non-NULL `NPC_type`. |
| `g_nav.cpp:1466` | Only entity 0's waypoint is kept up to date. NPCs chasing P2 need P2's. |
| `g_main.cpp:932, 952` | Entity 0 is skipped in the think loop and gets `ClientEndFrame`. P2 needs the same treatment, without running its `ps -> s` copy twice. |
| `g_utils.cpp:621` | `G_AddEvent` puts entity 0's events in `ps`, all others' in `s.event`. Fine on the host; the guest cgame (M1) expects its own events in `ps`. |

## Batch plan

Each batch is one subsystem and one commit. After each: build with `EF_COOP` OFF and run the
regression harness; build with `EF_COOP` ON, run with `sp_coop 0` and run it again. Both must be
identical to the baseline. Then a short `sp_coop 1` smoke test with the P2 puppet (from B1 on).

| Batch | Files | Edits | What it does | Co-op-on check |
|---|---|---|---|---|
| B0 | `efgame/CMakeLists.txt`, `EFAndroid-SP/app/jni/macos/CMakeLists.txt`, `macos/make-app.sh`, new `coop_public.h`, `src/coop/coop_main.cpp` | 0 call sites | `EF_COOP` option, `sp_coop` cvar, helper header | `sp_coop 1` with no P2 does nothing |
| B1 | `g_main.cpp`, `g_active.cpp`, `g_client.cpp`, `g_combat.cpp` (player_die), `g_savegame.cpp`, `cg_camera.cpp`, new `coop_player.cpp`, `coop_save.cpp`, `coop_cutscene.cpp` | 21 sites (7 hooks) | P2 puppet: spawn next to P1 (new game, load, map change, respawn), drive by console / "follow P1", die and respawn without reloading, hidden and non-solid in cameras, left out of saves | P2 appears, follows, survives a save that then loads in plain SP |
| B2 | `g_weapon.cpp`, `g_missile.cpp` | 23 | Any player | P2 can shoot; NPCs hear P2's shots |
| B3 | `g_combat.cpp` (G_Damage etc.) | 11 | Any player | P2 takes difficulty-scaled damage, does not die from falling |
| B4 | `bg_pmove.cpp`, `bg_panimate.cpp` | 15 | Any player / view client. Shared with cgame prediction | P2 footsteps alert NPCs; P2 can use buttons |
| B5 | `g_trigger.cpp`, `g_items.cpp`, `g_misc.cpp`, `g_breakable.cpp`, `g_turret.cpp` | 9 | Any player for PLAYERONLY triggers and pickups | P2 trips triggers; P2 picks up items |
| B6 | `AI_Scavenger.cpp`, `AI_Utils.cpp`, `AI_Vohrsoth.cpp`, `NPC_utils.cpp`, `NPC_combat.cpp`, `g_ambients.cpp` | 19 | Nearest player for enemy choice | Enemies attack whoever is closer |
| B7 | `NPC.cpp`, `NPC_spawn.cpp`, `NPC_behavior.cpp`, `g_navigator.cpp`, `g_nav.cpp` | 15 | Any player for "seen / in PVS"; P2 waypoint | NPCs are not removed or shy-spawned in P2's view |
| B8 | `NPC_reactions.cpp` | 5 | Any player for bump / use / greet | P2 can talk to crew |
| B9 | `g_items.cpp` (inventory), `Q3_Interface.cpp` (set weapon, player lock, Borg adapt), `g_client.cpp` (carry-over), `g_trigger.cpp` (activator remap) | 5 sites + hooks | Proposal's inventory rule, script weapons to both, `.coop` carry-over | Weapon pickup goes to both; `set weapon` gives both |
| B10 | `cg_draw.cpp`, `cg_event.cpp`, `cg_players.cpp`, `cg_scoreboard.cpp`, `cg_view.cpp`, `cg_weapons.cpp` | 19 | Local client in the cgame | None in M0 (`CG_LocalClientNum()` is 0 on the host). Can wait for M1 |
| M1 | `sp_bridge.cpp`, `cg_main.cpp`, `cg_camera.cpp:737`, `g_utils.cpp:621` | 8 | Guest-only changes | Belongs to the networking milestone |

B1 comes before the "any player" batches on purpose: without a P2 entity there is nothing to
test with co-op on, and without the save exclusion a co-op test save could get corrupted.

## Hooks for the other work areas

All are one line, `if (COOP_Active()) COOP_OnX(...);`, unless noted.

| Area | Where | Hook |
|---|---|---|
| P2 spawn | first `G_RunFrame` after `ClientBegin` (`g_client.cpp:538`); covers new game, load (`ReadLevel`, `g_savegame.cpp:961`), map change and holodeck respawn (`ClientSpawn`, `g_client.cpp:732`) | `COOP_RunFrame()` at the top of `G_RunFrame` (`g_main.cpp:831`) spawns P2 if missing, with traces for a clear spot next to P1, then calls `ClientThink(p2, &cmd)` |
| P2 frame | `g_main.cpp:932` (`i == 0` skip), `g_main.cpp:952` (`ClientEndFrame`) | P2 skipped in the think loop like entity 0; `COOP_EndFrame()` after `ClientEndFrame` |
| P2 death | `g_client.cpp:835` (die func), `g_active.cpp:1168` (respawn path), `g_combat.cpp:473` (`player_die`) | P2 die func; `COOP_P2DeadThink` respawns P2 next to P1 after a delay; game over stays P1-only |
| Cutscene hide | `cg_camera.cpp:61` `CGCam_Enable` (after lines 94-98), `cg_camera.cpp:107` `CGCam_Disable` (after 122-124) | `COOP_OnCameraEnable()` / `COOP_OnCameraDisable()`: P2 hidden (`SVF_NOCLIENT`), contents 0, velocity cleared, and back |
| Camera / lock freeze | `g_active.cpp:817` (`in_camera`), `Q3_Interface.cpp:6301` `Q3_SetPlayerLocked` | P2 frozen with P1 (`player_locked` and `in_camera` are globals; check their 25 readers) |
| Save exclusion | `g_savegame.cpp:931` `WriteLevel` start, `:648` `WriteGEntities` (both loops) | `COOP_PreSave()` clears or remaps pointers to P2 in other entities; both loops skip P2. `.coop` file written next to the `.sav` |
| Load | `g_savegame.cpp:961` `ReadLevel` end | `COOP_PostLoad()` reads `.coop`, P2 respawns next to P1 |
| Carry-over | `g_client.cpp:686` `Player_CacheForNextLevel`, `g_utils.cpp:942` `G_ChangeMap` | `COOP_CacheForNextLevel()` stores P2's stats for the `.coop` file |
| `Touch_Item` inventory | `g_items.cpp:209` (check at 232, pickup after `BG_CanItemBeGrabbed`), `Pickup_Weapon` `g_items.cpp:76` | `COOP_OnPickup(item, picker)`: weapons and holdables to both; targets fire once with `COOP_StoryActivator(picker)` |
| `Q3_SetWeapon` | `Q3_Interface.cpp:3420`, `CG_ChangeWeapon` at `:3485` | `COOP_OnScriptWeapon(ent, wp)`: P2 gets the weapon too; switch mirrored to the guest (M1) |
| Story activator | `g_trigger.cpp` `Touch_Multi` (147), `trigger_visible` (1184), `Touch_Item` | `COOP_StoryActivator(other)` when firing targets. Not for `trigger_push`/teleport, which act on the toucher |
| Borg adaptation | `Q3_Interface.cpp:1215`, `g_weapon.cpp:78` | Shared counter on P1 (recommended); reset clears P2 too |
| Nav | `g_nav.cpp:1466` `NAV_FindPlayerWaypoint` | also keeps P2's `waypoint` current |
| Camera `G_UseTargets` | `cg_camera.cpp:737` | `if (!COOP_IsGuest())` (M1; host unchanged) |

## What `EF_COOP` and `sp_coop` gate

**`EF_COOP` (CMake option, default OFF), build time:**
- `efgame/CMakeLists.txt`: `option(EF_COOP ... OFF)`; when ON, add `src/coop/*.cpp` to
  `libefgame` and define `EF_COOP` for the `efgame` target. The folder `src/coop/` is outside the
  existing globs (`game/ cgame/ icarus/ speedrun/`), so OFF builds never see those files.
- `EFAndroid-SP/app/jni/macos/CMakeLists.txt`: the same option, for the bridge-side files in M1
  (`sp/coop_*.cpp`, added to `sp_game_bridge`). M0 adds nothing here except the option.
- `macos/make-app.sh`: passes `-DEF_COOP=ON` to both cmake runs only for the "EF Test Coop" app
  (`org.voyagersp.testcoop`, port 27982). The Android build never sets it.
- With it OFF, `coop_public.h` turns every helper and hook into the old expression or nothing.
- The engine and the game library are separate files. In M1 the engine finds the game's co-op
  entry points with `dlsym` (absent = co-op unavailable). Do not add fields to
  `game_export_t`/`game_import_t`.

**`sp_coop` (latched cvar, default 0), run time, registered only in `EF_COOP` builds:**
- read once at `InitGame`; `COOP_Active()` is false unless it is 1;
- P2 spawn, drive, death and respawn;
- the co-op meaning of every helper (`COOP_IsPlayer` and the rest);
- every hook body (camera hide, locks, save exclusion, `.coop` file, inventory sharing,
  script weapons, activator remap, P2 waypoint);
- the co-op console commands (`coop_p2_*` to move P2 in M0);
- in M1, the co-op port and protocol.

## New files

| File | Content |
|---|---|
| `efgame/src/game/coop_public.h` | Helpers and hook declarations; macros when `EF_COOP` is off. The only co-op file included by existing code |
| `efgame/src/coop/coop_main.cpp` | `sp_coop` cvar, `COOP_Active`, init/shutdown, `COOP_RunFrame`, console commands |
| `efgame/src/coop/coop_player.cpp` | P2 entity: spawn placement, client setup (no NPC struct, non-NULL `NPC_type`, no `Munro` name), drive / follow P1, die and respawn, the "is player / nearest / sees" helpers |
| `efgame/src/coop/coop_cutscene.cpp` | Camera enable/disable, lock handling |
| `efgame/src/coop/coop_save.cpp` | Pre-save scrub and skip, `.coop` file, carry-over, post-load |
| `efgame/src/coop/coop_items.cpp` | Inventory rule, script weapons |
| `efgame/src/coop/coop_fixes.cpp` | Edge-case fix layer (M3; empty in M0) |
| `EFAndroid-SP/app/jni/sp/coop_bridge.cpp`, `coop_net.cpp` | M1: protocol, replica mode, guest snapshot |

## Things that make the proposal harder than it assumes

1. **The save format is frozen by struct size.** `WriteGEntities` writes `gentity_t` with
   `sizeof(tempEnt)` and the client with `sizeof(client)`. Any new field breaks loading in plain SP
   (and between OFF/ON builds). So: P2's number lives in the co-op module; no `EF_COOP`-only
   struct fields, ever.
2. **Leaving P2 out of a save is more than skipping it.** Other entities can point to P2 (`enemy`,
   `owner`, `activator`, `client->leader`/`follower`, ICARUS owners). Those pointers are saved as
   entity numbers; in plain SP they would point at an empty slot. `COOP_PreSave` must clear them
   or point them at P1 (enemies: P1; followers: drop).
3. **"Client with no NPC struct" already means "the player" in a few places**, besides entity 0:
   the save code (`g_savegame.cpp:348`) and the respawn path (`g_active.cpp:1168` →
   `load *respawn`). Both are crashes or game reloads for P2 if not guarded in B1.
4. **Only entity 0 gets `ClientEndFrame`, a current nav waypoint and `ps`-based events.** P2 is
   driven like an NPC (ClientThink from game code), so these need the B1/B7 hooks, and events need
   a decision in M1.
5. **`efgame/CMakeLists.txt` globs every `.cpp` in `game/`, and the hooks in shared files are
   still textual changes to sources the Android build also compiles.** With the macros the Android
   binary behaves the same, but "the Android build isn't touched" is true only for behavior, not
   for the source files. Co-op `.cpp` files go in `src/coop/`.
6. **`bg_pmove.cpp` is shared with cgame prediction**, where `pm->gent` is NULL: entity checks
   there are game-only and the helpers must take numbers.
7. **Game code reads cgame state for the shooter** (`cg_thirdPerson` in `g_weapon.cpp`, and the
   cgame writes into `g_entities[0]` in `cg_view.cpp:324` and `cg_camera.cpp`). On the host this
   stays P1; P2's view settings must come in through userinfo.
8. **Story sites need no change** (P1 is always entity 0), so the audit is smaller than planned:
   about 140 edits rather than 500.
9. **Scripts find the player by name (`Munro`), not by entity 0.** Good for the design ("the script
   player" is P1 by construction), but P2 must never get that name, and a script that runs on the
   activator still sees P2 unless the trigger remaps it.
10. **M0 needs no bridge change**, which lowers the risk for SP: all M0 work is in the game library.

## Site tables

Group names: story = story player (P1), any = `COOP_IsPlayer`, nearest = `COOP_NearestPlayer`,
local = local client, leave = no change, leave+hook = no change to the expression but a hook
next to it, dead = commented out or `#if 0`. Paths are under `efgame/src/` (`sp/` =
`EFAndroid-SP/app/jni/sp/`).

### B1. P2 puppet core, save exclusion, cutscene hide

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `cgame/cg_camera.cpp:94` | CGCam_Enable | `if ( &g_entities[0] && g_entities[0].client )` | story | keep; add `COOP_OnCameraEnable()` after | CGCam_Enable freezes P1 and makes it non-solid; hook hides P2 and makes it non-solid. |
| `cgame/cg_camera.cpp:97` | CGCam_Enable | `VectorClear( g_entities[0].client->ps.velocity );` | story | keep; add `COOP_OnCameraEnable()` after | CGCam_Enable freezes P1 and makes it non-solid; hook hides P2 and makes it non-solid. |
| `cgame/cg_camera.cpp:98` | CGCam_Enable | `g_entities[0].contents = 0;` | story | keep; add `COOP_OnCameraEnable()` after | CGCam_Enable freezes P1 and makes it non-solid; hook hides P2 and makes it non-solid. |
| `cgame/cg_camera.cpp:122` | CGCam_Disable | `if ( &g_entities[0] && g_entities[0].client )` | story | keep; add `COOP_OnCameraDisable()` after | Restores P2. |
| `cgame/cg_camera.cpp:124` | CGCam_Disable | `g_entities[0].contents = MASK_PLAYERSOLID;` | story | keep; add `COOP_OnCameraDisable()` after | Restores P2. |
| `game/g_active.cpp:817` | ClientThink_real | `if ( ent->s.number == 0 ) {` | any | split: camera freeze for `COOP_IsPlayer(ent)`, `ClientCinematicThink`/skip only for entity 0 | CARE: not a straight swap. Cinematic skip must stay P1-only. |
| `game/g_active.cpp:1168` | ClientThink_real | `if ( ent->NPC == NULL && level.time > client->respawnTime )` | leave+hook | `if (COOP_IsP2(ent)) { COOP_P2DeadThink(ent, ucmd); return; }` before it | CARE: any client without an NPC struct takes the player respawn path, and `respawn()` (g_client.cpp:307) runs `load *respawn`: a dead P2 pressing fire would reload the whole game. |
| `game/g_client.cpp:307` | respawn | `gi.SendConsoleCommand("load *respawn\n")` | leave+hook | never reached for P2 (guard above) |  |
| `game/g_client.cpp:829` | ClientSpawn | `ent->targetname = "Munro"; ent->script_targetname = "Munro";` | leave | none; P2 gets no targetname (or `coop_p2`) | Scripts find the player by the name `Munro`, so "the script player" is P1 by construction. P2 must never get that name. |
| `game/g_client.cpp:835` | ClientSpawn | `ent->e_DieFunc = dieF_player_die` | leave+hook | P2 gets `dieF_COOP_P2Die` (new die func enum entry) | CARE: adding a die-func enum value changes the save function tables; append it last and check the save code maps unknown values. Alternative: reuse an existing die func and branch inside player_die on `COOP_IsP2`. |
| `game/g_combat.cpp:488` | player_die | `if ( self->s.number == 0 && attacker && attacker->client && attacker->client->playerTea...` | story | keep `== 0`; P2 gets its own die func | player_die: mission failed text + game over. P2 must not use player_die (see hooks). |
| `game/g_combat.cpp:517` | player_die | `if ( self->s.number == 0 && attacker )` | story | keep `== 0`; P2 gets its own die func | player_die: mission failed text + game over. P2 must not use player_die (see hooks). |
| `game/g_main.cpp:932` | G_RunFrame | `if ( i == 0 )` | leave+hook | keep; P2 is driven by `COOP_RunFrame()` before the entity loop | "The player is ucmd driven": entity 0 skips think. P2 must be skipped the same way or its (empty) think/animate runs; it needs ClientThink every frame instead. |
| `game/g_main.cpp:952` | G_RunFrame | `ent = &g_entities[0];` | leave+hook | add `COOP_EndFrame()` after `ClientEndFrame(&g_entities[0])` | ClientEndFrame (damage feedback, ps->s copy) only runs for entity 0. P2 needs it too, but NPC clients get ps->s via ClientThink; check before calling it twice. |
| `game/g_savegame.cpp:348` | EnumerateField | `if (ent->NPC == NULL) { *(int *)pv = GetGClientNum(...) }` | leave+hook | never reached for P2 (P2 not written) | CARE: a client with no NPC struct is saved as an index into `level.clients`. P2's G_Alloc'd client would become a garbage index. Save exclusion is mandatory, not optional. |
| `game/g_savegame.cpp:650` | WriteGEntities | `for (int i=0; i<(qbAutosave?1:globals.num_entities); i++) (count + write loops, 650 and...` | leave+hook | `if (COOP_IsP2(ent)) continue;` in both loops | The NMED count and the write loop must skip the same entities. |
| `game/g_savegame.cpp:938` | WriteLevel | `gclient_t client = level.clients[0];` | leave | none (save hook elsewhere) | Writes/reads the one real client. P2 is not in `level.clients`. See save exclusion. |
| `game/g_savegame.cpp:980` | ReadLevel | `EvaluateFields(savefields_gClient, (byte *)&junkClient, (byte *)&level.clients[0], 'GCL...` | leave | none (save hook elsewhere) | Writes/reads the one real client. P2 is not in `level.clients`. See save exclusion. |
| `game/g_savegame.cpp:996` | ReadLevel | `EvaluateFields(savefields_gClient, (byte *)&GClient, (byte *)&level.clients[0], 'GCLI',...` | leave | none (save hook elsewhere) | Writes/reads the one real client. P2 is not in `level.clients`. See save exclusion. |
| `game/g_savegame.cpp:997` | ReadLevel | `level.clients[0] = GClient; // struct copy` | leave | none (save hook elsewhere) | Writes/reads the one real client. P2 is not in `level.clients`. See save exclusion. |
| `game/g_savegame.cpp:1009` | ReadLevel | `G_OldSaveRenumberWeapons( &level.clients[0], &g_entities[0].s );` | leave | none (save hook elsewhere) | Writes/reads the one real client. P2 is not in `level.clients`. See save exclusion. |

### B2. Weapons and missiles

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/g_missile.cpp:100` | G_MissileImpact | `if ( ent->owner && ent->owner->s.number == 0 )` | any | `COOP_IsPlayer(ent->owner)` |  |
| `game/g_missile.cpp:114` | G_MissileImpact | `if ( ent->owner && ent->owner->s.number == 0 )` | any | `COOP_IsPlayer(ent->owner)` |  |
| `game/g_missile.cpp:173` | G_MissileImpact | `if ( ent->owner && ent->owner->s.number == 0 )` | any | `COOP_IsPlayer(ent->owner)` |  |
| `game/g_missile.cpp:220` | G_ExplodeMissile | `if ( ent->owner && ent->owner->s.number == 0 )` | any | `COOP_IsPlayer(ent->owner)` |  |
| `game/g_weapon.cpp:151` | WP_FirePhaser | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Player muzzle/eye-point rules. |
| `game/g_weapon.cpp:220` | WP_FireCompressionRifle | `if ( ent->s.number == 0 && !cg_thirdPerson.integer )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` and `COOP_ThirdPerson(ent)` for `cg_thirdPerson.integer` | CARE: reads the host's cgame cvar for the shooter. For P2 it must be P2's own setting (sent in userinfo). Off: identical. |
| `game/g_weapon.cpp:276` | WP_FireCompressionRifle | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI sound events. |
| `game/g_weapon.cpp:300` | WP_FireCompressionRifle | `if ( ent->client->ps.clientNum != 0 )` | any | `!COOP_IsPlayerNum(ent->client->ps.clientNum)` | NPC damage halving. NPC ps.clientNum = s.number (NPC_spawn.cpp:502), so P2 also gets its number. |
| `game/g_weapon.cpp:337` | WP_FireIMOD | `if ( ent->s.number == 0 && !cg_thirdPerson.integer )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` and `COOP_ThirdPerson(ent)` for `cg_thirdPerson.integer` | CARE: reads the host's cgame cvar for the shooter. For P2 it must be P2's own setting (sent in userinfo). Off: identical. |
| `game/g_weapon.cpp:356` | WP_FireIMOD | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI sound events. |
| `game/g_weapon.cpp:481` | FireScavengerBullet | `if ( ent->client->ps.clientNum != 0 )` | any | `!COOP_IsPlayerNum(ent->client->ps.clientNum)` | NPC damage halving. NPC ps.clientNum = s.number (NPC_spawn.cpp:502), so P2 also gets its number. |
| `game/g_weapon.cpp:496` | FireScavengerBullet | `if ( ent->s.number != 0 )` | any | `!COOP_IsPlayer(ent)` |  |
| `game/g_weapon.cpp:582` | WP_FireScavenger | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Player muzzle/eye-point rules. |
| `game/g_weapon.cpp:798` | WP_FireStasisAlt | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI sound events. |
| `game/g_weapon.cpp:920` | grenadeSpewShrapnel | `if ( ent->owner && ent->owner->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Player muzzle/eye-point rules. |
| `game/g_weapon.cpp:1098` | FireTetrionBullet | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI sound events. |
| `game/g_weapon.cpp:1210` | WP_FireDreadnoughtBeam | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Player muzzle/eye-point rules. |
| `game/g_weapon.cpp:1225` | WP_FireDreadnoughtBeam | `if ( !traceEnt->takedamage && ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Player muzzle/eye-point rules. |
| `game/g_weapon.cpp:1372` | WP_DreadnoughtBurstThink | `if ( !traceEnt->takedamage && ent->owner && ent->owner->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Player muzzle/eye-point rules. |
| `game/g_weapon.cpp:1813` | WP_FireProtonGun | `if ( ent->s.number == 0 && !cg_thirdPerson.integer )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` and `COOP_ThirdPerson(ent)` for `cg_thirdPerson.integer` | CARE: reads the host's cgame cvar for the shooter. For P2 it must be P2's own setting (sent in userinfo). Off: identical. |
| `game/g_weapon.cpp:1869` | WP_FireProtonGun | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI sound events. |
| `game/g_weapon.cpp:1893` | WP_FireProtonGun | `if ( ent->client->ps.clientNum != 0 )` | any | `!COOP_IsPlayerNum(ent->client->ps.clientNum)` | NPC damage halving. NPC ps.clientNum = s.number (NPC_spawn.cpp:502), so P2 also gets its number. |
| `game/g_weapon.cpp:2954` | CalcMuzzlePoint | `if ( ent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Player muzzle/eye-point rules. |

### B3. Combat

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/g_combat.cpp:365` | G_SetMissionStatusText | `else if ( attacker && attacker->s.number != 0 && attacker->client && attacker->client->...` | any | `!COOP_IsPlayer(attacker)` |  |
| `game/g_combat.cpp:543` | player_die | `if ( self->s.number != 0 && !( contents & CONTENTS_NODROP ) && (self->s.weapon == WP_CO...` | any | `!COOP_IsPlayer(self)` |  |
| `game/g_combat.cpp:1102` | G_FriendlyFireReaction | `if ( attacker != &g_entities[0] \|\| targ == &g_entities[0] )` | any | `!COOP_IsPlayer(attacker) \|\| COOP_IsPlayer(targ)` | CARE: today it compares pointers, not numbers. Equal when off. With co-op on, P1 shooting P2 must not count as friendly fire against an NPC (P2 has no NPC struct, so line 1107 already returns). |
| `game/g_combat.cpp:1327` | G_Damage | `if ( attacker->s.number != 0 && damage >= 2 && targ->s.number != 0 && attacker->client ...` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 1610 is the difficulty damage scale to the player; 1686 runs the pain script (P2 has none). |
| `game/g_combat.cpp:1364` | G_Damage | `if ( targ->s.number != 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 1610 is the difficulty damage scale to the player; 1686 runs the pain script (P2 has none). |
| `game/g_combat.cpp:1435` | G_Damage | `if ( attacker->s.number != 0 && targ->s.number != 0 &&//player not involved in any way ...` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 1610 is the difficulty damage scale to the player; 1686 runs the pain script (P2 has none). |
| `game/g_combat.cpp:1442` | G_Damage | `if ( targ->s.number == 0 &&//player was hit` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 1610 is the difficulty damage scale to the player; 1686 runs the pain script (P2 has none). |
| `game/g_combat.cpp:1564` | G_Damage | `if ( mod == MOD_FALLING && targ->s.number == 0 && targ->health < 1 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Falling never kills a player. |
| `game/g_combat.cpp:1610` | G_Damage | `if ( targ->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 1610 is the difficulty damage scale to the player; 1686 runs the pain script (P2 has none). |
| `game/g_combat.cpp:1639` | G_Damage | `if ( (targ->flags&FL_UNDYING) && !(dflags&DAMAGE_NO_PROTECTION) && attacker->s.number !...` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 1610 is the difficulty damage scale to the player; 1686 runs the pain script (P2 has none). |
| `game/g_combat.cpp:1686` | G_Damage | `if ( targ->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 1610 is the difficulty damage scale to the player; 1686 runs the pain script (P2 has none). |

### B4. Movement and animation (shared with cgame prediction)

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/bg_panimate.cpp:372` | PM_TorsoAnimation | `else if((pm->ps->legsAnim & ~ANIM_TOGGLEBIT) == BOTH_CROUCH1IDLE && pm->ps->clientNum !...` | any | `!COOP_IsPlayerNum(pm->ps->clientNum)` | Runs in cgame prediction too (gent NULL there): helper must take a number. |
| `game/bg_panimate.cpp:384` | PM_TorsoAnimation | `if(pm->gent != NULL && pm->gent->s.number == 0)` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` |  |
| `game/bg_pmove.cpp:215` | PM_Accelerate | `if (pm->ps->clientNum == 0) {` | local | `pm->ps->clientNum == COOP_ViewClientNum()` | Strafe helper HUD feeds the local view. Off/host: 0. |
| `game/bg_pmove.cpp:1014` | PM_CrashLand | `if ( pm->gent && pm->gent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI hears footsteps/landings of any player. |
| `game/bg_pmove.cpp:1032` | PM_CrashLand | `if ( pm->gent && pm->gent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI hears footsteps/landings of any player. |
| `game/bg_pmove.cpp:1048` | PM_CrashLand | `if ( pm->gent && pm->gent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI hears footsteps/landings of any player. |
| `game/bg_pmove.cpp:1739` | PM_Footsteps | `if ( pm->gent && pm->gent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI hears footsteps/landings of any player. |
| `game/bg_pmove.cpp:1754` | PM_Footsteps | `if ( pm->gent && pm->gent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI hears footsteps/landings of any player. |
| `game/bg_pmove.cpp:1771` | PM_Footsteps | `if ( pm->gent && pm->gent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI hears footsteps/landings of any player. |
| `game/bg_pmove.cpp:1787` | PM_Footsteps | `if ( pm->gent && pm->gent->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | AI hears footsteps/landings of any player. |
| `game/bg_pmove.cpp:1853` | G_WeaponChangeEffect | `if ( ent->s.number == 0 \|\| strstr( ent->client->renderInfo.torsoModelName , "hazard" ) ...` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | CARE: the `\|\|` chain then calls `Q_stricmp(..., ent->NPC_type)`; P2 must have a non-NULL `NPC_type` or this crashes. |
| `game/bg_pmove.cpp:2041` | PM_Weapon | `if(pm->gent && pm->gent->s.number == 0)` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 2618 is PM_Use: the use key for P2. |
| `game/bg_pmove.cpp:2351` | PM_SetSpecialMoveValues | `if ( pm->ps->clientNum == 0 )` | local | `== COOP_ViewClientNum()` | Slow-mo matrix mode = view effect. |
| `game/bg_pmove.cpp:2500` | Pmove | `if ( cg_drawOverbounceInfo.integer && pm->ps->clientNum == 0 ) {` | local | `== COOP_ViewClientNum()` | Debug HUD. |
| `game/bg_pmove.cpp:2618` | Pmove | `if(pm->gent && pm->gent->s.number == 0)` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | 2618 is PM_Use: the use key for P2. |

### B5. Triggers, items, world

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/g_breakable.cpp:288` | Touch_AssimilationTrigger | `if ( other->s.number != 0 )` | any | `!COOP_IsPlayer(other)` |  |
| `game/g_items.cpp:232` | Touch_Item | `if (!other->s.number == 0) // Not the player?` | any | `!COOP_IsPlayer(other)` | Today `!other->s.number == 0` is `(!n)==0`, i.e. `n != 0`; replacement keeps that. Plus the inventory hook. |
| `game/g_misc.cpp:1311` | touch_ammo_crystal_tigger | `if ( !other->s.number == 0 )` | any | `!COOP_IsPlayer(other)` | Same precedence quirk. |
| `game/g_trigger.cpp:147` | Touch_Multi | `if ( other->s.number != 0 )` | any | `!COOP_IsPlayer(other)` | PLAYERONLY. Activator remap to P1 for story triggers: see hooks. |
| `game/g_trigger.cpp:225` | Touch_Multi | `if( other->s.number == 0 && ( other->client->ps.weapon > MAX_PLAYER_WEAPONS \|\| other->c...` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` |  |
| `game/g_trigger.cpp:550` | trigger_push_touch | `if ( other->s.number != 0 )` | any | `!COOP_IsPlayer(other)` | PLAYERONLY. Activator remap to P1 for story triggers: see hooks. |
| `game/g_trigger.cpp:1112` | trigger_entdist_use | `found = &g_entities[0];` | nearest | `COOP_NearestPlayer(owner->currentOrigin)` | trigger_entdist "player within N": nearest is the right answer for "any player within". |
| `game/g_trigger.cpp:1184` | trigger_visible_check_player_visibility | `gentity_t *player = &g_entities[0];` | any | loop `COOP_ForEachPlayer`, fire with `COOP_StoryActivator(p)` | trigger_visible: any player looking at it. CARE: fires once and frees itself; first seer wins. |
| `game/g_turret.cpp:1153` | stasis_shooter_active | `if ( radiusEnts[i]->s.number == 0 )` | any | `COOP_IsPlayer(radiusEnts[i])` |  |

### B6. AI target choice

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/AI_Scavenger.cpp:317` | NPC_BSScav_Sleep | `G_SetEnemy( NPC, &g_entities[0] );` | nearest | `G_SetEnemy(NPC, COOP_NearestPlayer(NPC->currentOrigin))` |  |
| `game/AI_Scavenger.cpp:340` | NPC_CheckEnemyStealth | `gentity_t *target = &g_entities[0]; //Change this pointer to assess other entities` | nearest | `COOP_NearestPlayer(NPC->currentOrigin)` |  |
| `game/AI_Scavenger.cpp:508` | NPC_Scav_InvestigateEvent | `G_SetEnemy( NPC, &g_entities[0] );` | nearest | `G_SetEnemy(NPC, COOP_NearestPlayer(NPC->currentOrigin))` |  |
| `game/AI_Utils.cpp:211` | AI_DistributeAttack | `if ( enemy != &g_entities[0] )` | any | `!COOP_IsPlayer(enemy)` |  |
| `game/AI_Utils.cpp:213` | AI_DistributeAttack | `int aroundPlayer = AI_GetGroupSize( g_entities[0].currentOrigin, 48, team, attacker );` | any | `enemy->currentOrigin` | Exactly equal when off: enemy == &g_entities[0] at this point. |
| `game/AI_Utils.cpp:218` | AI_DistributeAttack | `return &g_entities[0];` | any | `return enemy;` | Same. |
| `game/AI_Vohrsoth.cpp:370` | Vohrsoth_CheckEnemy | `gentity_t *target = &g_entities[0]; //FIXME: If you want something else, put it here` | nearest | `COOP_NearestPlayer(NPC->currentOrigin)` | Boss fight. |
| `game/NPC_combat.cpp:129` | G_SetEnemy | `if ( self->client->playerTeam == TEAM_STARFLEET && enemy->s.number == 0 )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Starfleet NPC vs player. |
| `game/NPC_combat.cpp:967` | NPC_PickEnemy | `newenemy = &g_entities[0];` | nearest | `COOP_NearestPlayer(NPC->currentOrigin)` |  |
| `game/NPC_utils.cpp:1030` | NPC_PickEnemyExt | `if ( event->owner == &g_entities[0] )` | any | `COOP_IsPlayer(event->owner)` |  |
| `game/NPC_utils.cpp:1050` | NPC_FindPlayer | `return NPC_TargetVisible( &g_entities[0] );` | nearest | `NPC_TargetVisible(COOP_NearestPlayer(NPC->currentOrigin))` | CARE: "nearest" may not be the visible one; co-op on could try both. Off: identical. |
| `game/NPC_utils.cpp:1066` | NPC_CheckPlayerDistance | `if ( NPC->enemy->s.number == 0 )` | nearest | `p = COOP_NearestPlayer(...)`, then `NPC->enemy == p`, `InFOV(p,...)`, `G_SetEnemy(NPC,p)` | CARE: 1066 becomes `COOP_IsPlayer(NPC->enemy)` and the rest use the nearest player. Convert as one unit. |
| `game/NPC_utils.cpp:1070` | NPC_CheckPlayerDistance | `if ( InFOV( &g_entities[0], NPC, NPCInfo->stats.hfov, NPCInfo->stats.vfov ) == qfalse )` | nearest | `p = COOP_NearestPlayer(...)`, then `NPC->enemy == p`, `InFOV(p,...)`, `G_SetEnemy(NPC,p)` | CARE: 1066 becomes `COOP_IsPlayer(NPC->enemy)` and the rest use the nearest player. Convert as one unit. |
| `game/NPC_utils.cpp:1075` | NPC_CheckPlayerDistance | `if ( distance > DistanceSquared( NPC->currentOrigin, g_entities[0].currentOrigin ) )` | nearest | `p = COOP_NearestPlayer(...)`, then `NPC->enemy == p`, `InFOV(p,...)`, `G_SetEnemy(NPC,p)` | CARE: 1066 becomes `COOP_IsPlayer(NPC->enemy)` and the rest use the nearest player. Convert as one unit. |
| `game/NPC_utils.cpp:1077` | NPC_CheckPlayerDistance | `G_SetEnemy( NPC, &g_entities[0] );` | nearest | `p = COOP_NearestPlayer(...)`, then `NPC->enemy == p`, `InFOV(p,...)`, `G_SetEnemy(NPC,p)` | CARE: 1066 becomes `COOP_IsPlayer(NPC->enemy)` and the rest use the nearest player. Convert as one unit. |
| `game/g_ambients.cpp:531` | flier_find_enemy | `gi.trace( &tr, self->currentOrigin, vec3_origin, vec3_origin, g_entities[0].currentOrig...` | nearest | `p = COOP_NearestPlayer(self->currentOrigin)` then use `p` |  |
| `game/g_ambients.cpp:533` | flier_find_enemy | `if ( tr.fraction == 1.0f \|\| tr.entityNum == g_entities[0].s.number )` | nearest | `p = COOP_NearestPlayer(self->currentOrigin)` then use `p` |  |
| `game/g_ambients.cpp:535` | flier_find_enemy | `G_SetEnemy(self, &g_entities[0]);` | nearest | `p = COOP_NearestPlayer(self->currentOrigin)` then use `p` |  |
| `game/g_ambients.cpp:619` | flier_fire_children | `VectorCopy( g_entities[0].currentOrigin, curCheck->pos1 );` | nearest | `p = COOP_NearestPlayer(self->currentOrigin)` then use `p` |  |

### B7. AI perception and navigation

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/NPC.cpp:66` | NPC_RemoveBody | `if ( DistanceSquared( g_entities[0].currentOrigin, self->currentOrigin ) <= REMOVE_DIST...` | any | `COOP_AnyPlayerSees(self, REMOVE_DISTANCE_SQR, 110, 90)` | NPC removal only when NO player sees it. New loop helper; off = the same three checks on entity 0, in the same order. |
| `game/NPC.cpp:71` | NPC_RemoveBody | `if ( (InFOV( self, &g_entities[0], 110, 90 )) ) // generous FOV check` | any | `COOP_AnyPlayerSees(self, REMOVE_DISTANCE_SQR, 110, 90)` | NPC removal only when NO player sees it. New loop helper; off = the same three checks on entity 0, in the same order. |
| `game/NPC.cpp:73` | NPC_RemoveBody | `if ( (NPC_ClearLOS( &g_entities[0], self->currentOrigin )) )` | any | `COOP_AnyPlayerSees(self, REMOVE_DISTANCE_SQR, 110, 90)` | NPC removal only when NO player sees it. New loop helper; off = the same three checks on entity 0, in the same order. |
| `game/NPC.cpp:332` | NPC_DrawSquadPath | `if ( gi.inPVS(squadPath->waypoints[i].origin, g_entities[0].currentOrigin) )` | any | `COOP_AnyPlayerInPVS(org)` |  |
| `game/NPC.cpp:363` | NPC_ShowDebugInfo | `if ( gi.inPVS( found->currentOrigin, g_entities[0].currentOrigin ) )` | any | `COOP_AnyPlayerInPVS(org)` |  |
| `game/NPC_behavior.cpp:2718` | NPC_BSRemove | `if( !gi.inPVS( NPC->currentOrigin, g_entities[0].currentOrigin ) )//FIXME: use cg.vieworg?` | any | `!COOP_AnyPlayerInPVS(NPC->currentOrigin)` |  |
| `game/NPC_spawn.cpp:1013` | NPC_ShySpawn | `if ( DistanceSquared( g_entities[0].currentOrigin, ent->currentOrigin ) <= SHY_SPAWN_DI...` | any | `COOP_AnyPlayerSees(ent, SHY_SPAWN_DISTANCE_SQR, 80, 64)` | Shy spawn. |
| `game/NPC_spawn.cpp:1016` | NPC_ShySpawn | `if ( (InFOV( ent, &g_entities[0], 80, 64 )) ) // FIXME: hardcoded fov` | any | `COOP_AnyPlayerSees(ent, SHY_SPAWN_DISTANCE_SQR, 80, 64)` | Shy spawn. |
| `game/NPC_spawn.cpp:1017` | NPC_ShySpawn | `if ( (NPC_ClearLOS( &g_entities[0], ent->currentOrigin )) )` | any | `COOP_AnyPlayerSees(ent, SHY_SPAWN_DISTANCE_SQR, 80, 64)` | Shy spawn. |
| `game/g_nav.cpp:54` | NPC_Blocked | `if ( ( blocker->s.number == 0 ) && ( ( blocker->client->playerTeam == self->client->pla...` | any | `COOP_IsPlayer(blocker)` |  |
| `game/g_nav.cpp:628` | NAV_ResolveEntityCollision | `if ( blocker->s.number == 0 )` | any | `COOP_IsPlayer(blocker)` |  |
| `game/g_nav.cpp:825` | NAV_MoveToGoal | `if ( self->NPC->goalEntity->s.number == 0 )` | any | `COOP_IsPlayer(self->NPC->goalEntity)` |  |
| `game/g_nav.cpp:1466` | NAV_FindPlayerWaypoint | `g_entities[0].waypoint = navigator.GetNearestNode( &g_entities[0], g_entities[0].lastWa...` | leave+hook | keep; `COOP_FindPlayerWaypoints()` also updates P2 | NAV_FindPlayerWaypoint keeps only entity 0's `waypoint` current. NPCs chasing P2 path to `enemy->waypoint`, so P2 needs it too. Called from G_RunFrame `i == 0`. |
| `game/g_navigator.cpp:748` |  | `if ( gi.inPVS( g_entities[0].currentOrigin, position ) )` | any | `COOP_AnyPlayerInPVS(...)` | Nav cheats when unseen; with two players, "unseen" means unseen by both. |
| `game/g_navigator.cpp:794` |  | `if ( ( gi.inPVS( g_entities[0].currentOrigin, start ) == false ) && ( gi.inPVS( g_entit...` | any | `COOP_AnyPlayerInPVS(...)` | Nav cheats when unseen; with two players, "unseen" means unseen by both. |

### B8. NPC reactions

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/NPC_reactions.cpp:63` | NPC_CheckAttacker | `if ( other == &g_entities[0] )` | any | `COOP_IsPlayer(other)` |  |
| `game/NPC_reactions.cpp:590` | NPC_UseResponse | `if ( user->s.number != 0 )` | any | `!COOP_IsPlayer(user)` | Use key on NPCs. |
| `game/NPC_reactions.cpp:625` | NPC_UseResponse | `if ( !noGreet && user->s.number == 0 && G_PlayerGreet( self, useWhenDone ) )` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Greeting plays Munro's voice line from the user; P2 shares Munro's voice. Acceptable for M0. |
| `game/NPC_reactions.cpp:671` | NPC_Use | `else if ( !self->enemy && activator->s.number == 0 && !gi.S_Override[self->s.number] &&...` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Greeting plays Munro's voice line from the user; P2 shares Munro's voice. Acceptable for M0. |
| `game/NPC_reactions.cpp:684` | NPC_Use | `else if ( !self->enemy && activator->s.number == 0 && !gi.S_Override[self->s.number] &&...` | any | `COOP_IsPlayer(x)` for `x->s.number == 0` | Greeting plays Munro's voice line from the user; P2 shares Munro's voice. Acceptable for M0. |

### B9. Gameplay hooks

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `game/Q3_Interface.cpp:1215` | Q3_ResetBorgAdaptHits | `ent = &g_entities[0];` | story | keep; `COOP_OnResetBorgAdapt()` also clears P2 | Borg adaptation counts live on entity 0 (and g_weapon.cpp:78). Decide: shared (P1's counter, recommended) or per-player. |
| `game/Q3_Interface.cpp:3485` | Q3_SetWeapon | `CG_ChangeWeapon( wp );` | leave+hook | add `COOP_OnScriptWeapon(ent, wp)` | Gives the weapon to P2 too; the weapon switch is mirrored to the guest in M1. |
| `game/Q3_Interface.cpp:6303` | Q3_SetPlayerLocked | `gentity_t *ent = &g_entities[0];` | story | keep; `COOP_OnPlayerLocked(locked)` locks P2 | `player_locked` is global; check every reader handles P2 (g_active/bg_pmove). |
| `game/g_client.cpp:691` | Player_CacheForNextLevel | `gentity_t *ent = &g_entities[0];` | story | keep; add `COOP_CacheForNextLevel()` | P1 carry-over; P2 carry-over goes to the `.coop` file. |
| `game/g_items.cpp:209` | Touch_Item | `(pickup body, after the 232 check)` | leave+hook | `COOP_OnPickup(ent, other)` after the pickup, targets fired with `COOP_StoryActivator(other)` | Shared weapons/holdables; first-come ammo/health/armour. |

### B10. cgame local client

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `cgame/cg_draw.cpp:844` | CG_DrawTeleportEffects | `cent = &cg_entities[0];` | local | `&cg_entities[CG_LocalClientNum()]` |  |
| `cgame/cg_draw.cpp:846` | CG_DrawTeleportEffects | `if ( cg.snap->ps.clientNum != 0 \|\| cg.renderingThirdPerson \|\| !cent->gent )` | local | `cg.snap->ps.clientNum != CG_LocalClientNum()` | On a guest the snapshot ps is P2, so the literal 0 would hide the effect. |
| `cgame/cg_draw.cpp:1758` | CG_DrawCrosshairNames | `centity_t *player = &cg_entities[0];` | local | `&cg_entities[CG_LocalClientNum()]` |  |
| `cgame/cg_draw.cpp:2014` | CG_DrawTED | `centity_t *player = &cg_entities[0];` | local | `&cg_entities[CG_LocalClientNum()]` |  |
| `cgame/cg_draw.cpp:2088` | CG_DrawTED | `if ( cg.snap->entities[ num ].number == 0)` | local | `== CG_LocalClientNum()` | TED radar skips self; P1 must show up on P2's radar. |
| `cgame/cg_draw.cpp:2330` | CG_DrawAssimilation | `if ( g_entities[0].s.eFlags & EF_ASSIMILATED )` | local | `CG_LocalGEnt()->s.eFlags` |  |
| `cgame/cg_event.cpp:159` | CG_EntityEvent | `if ( cent->gent && cent->gent->s.number == 0 && !cg_thirdPerson.integer )` | local | `cent->gent->s.number == CG_LocalClientNum()` |  |
| `cgame/cg_event.cpp:170` | CG_EntityEvent | `if ( cent->gent && cent->gent->s.number == 0 && !cg_thirdPerson.integer )` | local | `cent->gent->s.number == CG_LocalClientNum()` |  |
| `cgame/cg_players.cpp:843` | CG_RunLerpFrame | `if ( entNum == 0 )` | local | `entNum == CG_LocalClientNum()` |  |
| `cgame/cg_players.cpp:1056` | CG_PlayerAnimSounds | `if ( entNum == 0 && !cg_thirdPerson.integer )` | local | `entNum == CG_LocalClientNum()` |  |
| `cgame/cg_players.cpp:1548` | CG_PlayerAngles | `if ( cg.renderingThirdPerson && cent->gent && cent->gent->s.number == 0 )` | local | `== CG_LocalClientNum()` |  |
| `cgame/cg_players.cpp:3148` | CG_Player | `if(cent->currentState.number == 0 && !cg_thirdPerson.integer )` | local | `cent->currentState.number == CG_LocalClientNum()` | Skips drawing "my" body in first person. Getting it wrong hides P1 on the guest. |
| `cgame/cg_players.cpp:3291` | CG_Player | `if ( cent->currentState.weapon == WP_BORG_ASSIMILATOR && cent->gent->client->buttons & ...` | local | `cent->gent->enemy->s.number == CG_LocalClientNum()` | Assimilator wire aims at `cg.refdef.vieworg` when the target is "me"; for the other player it should use their tag. |
| `cgame/cg_players.cpp:3298` | CG_Player | `if ( cent->gent->enemy->s.number == 0 )` | local | `cent->gent->enemy->s.number == CG_LocalClientNum()` | Assimilator wire aims at `cg.refdef.vieworg` when the target is "me"; for the other player it should use their tag. |
| `cgame/cg_scoreboard.cpp:418` | TourScoreboard_MenuBlinkies | `gentity_t *ent = &g_entities[0];` | local | `CG_LocalGEnt()` | Mission stats screen (kills, accuracy). Per-player stats in co-op. |
| `cgame/cg_scoreboard.cpp:750` | Scoreboard_MenuBlinkies | `gentity_t *ent = &g_entities[0];` | local | `CG_LocalGEnt()` | Mission stats screen (kills, accuracy). Per-player stats in co-op. |
| `cgame/cg_view.cpp:324` | CG_OffsetThirdPersonOverheadView | `g_entities[0].client->ps.delta_angles[PITCH] = 0;` | local | `CG_LocalGEnt()->client->ps.delta_angles` | cgame writes into a game entity. On the host this must stay P1. |
| `cgame/cg_view.cpp:495` | CG_OffsetFirstPersonView | `centity_t *playerCent = &cg_entities[0];` | local | `&cg_entities[CG_LocalClientNum()]` |  |
| `cgame/cg_weapons.cpp:954` | CG_ChangeWeapon | `gentity_t *player = &g_entities[0];` | local | `CG_LocalGEnt()` | Weapon select reads STAT_WEAPONS. |

### M1 (guest only, not M0)

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `cgame/cg_camera.cpp:737` | CGCam_Update | `G_UseTargets( trackEnt, trackEnt );` | leave+hook | `if (!COOP_IsGuest())` | Game logic inside the cgame. Host unchanged; off on the guest. |
| `cgame/cg_main.cpp:1406` | CG_Init | `cg_entities[i].gent = &g_entities[i];` | leave | none | Host: real entities. Guest (M1): the mirrored copies. |
| `game/g_utils.cpp:621` | G_AddEvent | `if ( !ent->s.number ) //only one client` | leave | none in M0 | G_AddEvent: entity 0 gets events in `ps`, everyone else in `s.event`. P2 events land in `s`; the guest cgame (M1) expects its own events in `ps`. Revisit with the guest snapshot. |
| `sp/sp_bridge.cpp:767` | SP_SettleClient | `gentity_t* p0 = SP_GEntity(0); ... ge->ClientThink(0, &g_lastcmd)` | leave | none in M0 | P2 is driven from game code in M0, so the bridge needs no change. |
| `sp/sp_bridge.cpp:840` | BuildSnapshot | `gentity_t* p0 = SP_GEntity(0); g_snap.ps = *(playerState_t*)p0->client;` | local | `SP_GEntity(SP_LocalClientNum())` | Guest builds its snapshot from P2. |
| `sp/sp_bridge.cpp:854` | BuildSnapshot | `if( i != 0 && e->s.loopSound && !SP_EntInView(...) )` | local | `i != SP_LocalClientNum()` |  |
| `sp/sp_bridge.cpp:886` | SP_CgameConsoleCmd | `ge->ClientCommand(0)` | leave | none | Host console = P1. Guest commands arrive over the co-op protocol for P2. |
| `sp/sp_bridge.cpp:1124` | (cgame syscall) | `ge->ClientCommand(0)` | leave | none | Same. |

### No change: story player, leave as is, dead code

| Site | Function | Current | Group | Replacement | Risk / note |
|---|---|---|---|---|---|
| `cgame/cg_main.cpp:811` | CG_RegisterClientModels | `if(entityNum < MAX_CLIENTS)` | leave | none | `cgs.clientinfo[MAX_CLIENTS]` slot; P2 uses `gent->client->clientInfo` like NPCs. |
| `cgame/cg_main.cpp:1263` | CG_RegisterGraphics | `for (i=0 ; i<MAX_CLIENTS ; i++)` | leave | none | `cgs.clientinfo[MAX_CLIENTS]` slot; P2 uses `gent->client->clientInfo` like NPCs. |
| `cgame/cg_media.h:361` |  | `clientInfo_t clientinfo[MAX_CLIENTS];` | leave | none | MAX_CLIENTS stays 1. |
| `cgame/cg_players.cpp:132` | CG_CustomSound | `ci = &g_entities[0].client->clientInfo;` | leave | none | Fallback to Munro sounds. |
| `cgame/cg_players.cpp:3649` | CG_ResetPlayerEntity | `if(cent->currentState.clientNum < MAX_CLIENTS)` | leave | none | clientNum<MAX_CLIENTS picks cgs.clientinfo; P2 (num>=1) takes the NPC path, which needs its clientInfo loaded at spawn. |
| `cgame/cg_scoreboard.cpp:997` | Scoreboard_Draw | `player = g_entities[0];` | dead | none | Commented out / `#if 0`. |
| `cgame/cg_servercmds.cpp:61` | CG_ConfigStringModified | `else if ( num >= CS_PLAYERS && num < CS_PLAYERS+MAX_CLIENTS ) {` | leave | none |  |
| `game/G_Timer.cpp:32` | TIMER_Save | `for ( j = 0, ent = &g_entities[0]; j < MAX_GENTITIES; j++, ent++ )` | leave | none | Loop start. |
| `game/G_Timer.cpp:68` | TIMER_Load | `for ( j = 0, ent = &g_entities[0]; j < MAX_GENTITIES; j++, ent++ )` | leave | none | Loop start. |
| `game/NPC_behavior.cpp:1299` | NPC_BSMedic | `int lowest = g_entities[0].max_health;` | story | none | Only reads max_health as a threshold value. |
| `game/NPC_behavior.cpp:1391` | NPC_BSMedicHide | `ok_level = g_entities[0].max_health;` | story | none | Only reads max_health as a threshold value. |
| `game/NPC_behavior.cpp:1578` | NPC_BSMedicCombat | `ok_level = g_entities[0].max_health;` | story | none | Only reads max_health as a threshold value. |
| `game/NPC_combat.cpp:1760` | NPC_FindSquadPoint | `//float playerDist = DistanceSquared( g_entities[0].currentOrigin, NPC->currentOrigin );` | dead | none | Commented out / `#if 0`. |
| `game/NPC_formation.cpp:532` | NPC_BlockingPlayer | `if ( VectorNormalize2( g_entities[0].client->ps.velocity, velocityDir ) == 0 )` | story | none | Squad formation follows P1 (proposal). `COOP_StoryPlayer()` is literally `&g_entities[0]` forever. |
| `game/NPC_formation.cpp:536` | NPC_BlockingPlayer | `VectorSubtract( NPC->currentOrigin, g_entities[0].currentOrigin, playerDir );` | story | none | Squad formation follows P1 (proposal). `COOP_StoryPlayer()` is literally `&g_entities[0]` forever. |
| `game/NPC_formation.cpp:559` | NPC_BlockingPlayer | `VectorMA( g_entities[0].currentOrigin, distance, velocityDir, testPos );` | story | none | Squad formation follows P1 (proposal). `COOP_StoryPlayer()` is literally `&g_entities[0]` forever. |
| `game/NPC_formation.cpp:564` | NPC_BlockingPlayer | `VectorAdd( testPos, g_entities[0].mins, ptmins );` | story | none | Squad formation follows P1 (proposal). `COOP_StoryPlayer()` is literally `&g_entities[0]` forever. |
| `game/NPC_formation.cpp:565` | NPC_BlockingPlayer | `VectorAdd( testPos, g_entities[0].maxs, ptmaxs );` | story | none | Squad formation follows P1 (proposal). `COOP_StoryPlayer()` is literally `&g_entities[0]` forever. |
| `game/NPC_formation.cpp:2091` | NPC_TryReenlistFormation | `gentity_t *leader = &g_entities[0]; // EF squad formations are always player-led` | story | none |  |
| `game/NPC_reactions.cpp:384` | NPC_Touch | `if ( other->s.number == 0 && self->client->playerTeam == other->client->playerTeam )` | dead | none | Commented out / `#if 0`. |
| `game/NPC_reactions.cpp:697` | NPC_CheckPlayerAim | `gentity_t *player = &g_entities[0];` | dead | none | Commented out / `#if 0`. |
| `game/NPC_spawn.cpp:5684` | NPC_Spawn_f | `AngleVectors(g_entities[0].client->ps.viewangles, forward, NULL, NULL);` | story | none | `npc spawn` console command in front of the host. |
| `game/NPC_spawn.cpp:5686` | NPC_Spawn_f | `VectorMA(g_entities[0].currentOrigin, 64, forward, end);` | story | none | `npc spawn` console command in front of the host. |
| `game/NPC_spawn.cpp:5687` | NPC_Spawn_f | `gi.trace(&trace, g_entities[0].currentOrigin, NULL, NULL, end, 0, MASK_SOLID);` | story | none | `npc spawn` console command in front of the host. |
| `game/NPC_utils.cpp:925` | NPC_GetCheckDelta | `int distance = DistanceSquared( NPC->currentOrigin, g_entities[0].currentOrigin );` | dead | none | Commented out / `#if 0`. |
| `game/NPC_utils.cpp:1005` | NPC_PickEnemyExt | `return &g_entities[0];` | dead | none | Commented out / `#if 0`. |
| `game/Q3_Interface.cpp:948` | Q3_CheckTourSuccess | `client = &level.clients[0];` | story | none | Objectives/tour state lives in `level.clients[0]`. Mirrored to the guest in M1. |
| `game/Q3_Interface.cpp:1060` | Q3_CheckTourSuccess | `G_UseTargets2( &g_entities[0], &g_entities[0], "objectivecheck" );` | story | none | Objectives/tour state lives in `level.clients[0]`. Mirrored to the guest in M1. |
| `game/Q3_Interface.cpp:1077` | Q3_SetObjective | `client = &level.clients[0];` | story | none | Objectives/tour state lives in `level.clients[0]`. Mirrored to the guest in M1. |
| `game/Q3_Interface.cpp:1201` | Q3_ObjectiveClearAll | `client = &level.clients[0];` | story | none | Objectives/tour state lives in `level.clients[0]`. Mirrored to the guest in M1. |
| `game/Q3_Interface.cpp:1396` | HeadText | `if ( entID == 0 && precacheWav[index].speaker != SP_MUNRO )` | leave | none | Speaker check for subtitles. |
| `game/Q3_Interface.cpp:1419` | Q3_PlaySound | `gentity_t *ent2 = &g_entities[0];` | story | none | Sound relative to P1; mirrored as an event in M2. |
| `game/Q3_Interface.cpp:2621` | Q3_SetPlayerTeam | `if ( ent->s.number == 0 )` | story | none | Script target is the entity named `Munro` = P1 only. P2 must never get that targetname. |
| `game/Q3_Interface.cpp:2718` | Q3_SetHealth | `if ( ent->s.number == 0 )` | story | none | Script target is the entity named `Munro` = P1 only. P2 must never get that targetname. |
| `game/Q3_Interface.cpp:2764` | Q3_SetArmor | `if ( ent->s.number == 0 )` | story | none | Script target is the entity named `Munro` = P1 only. P2 must never get that targetname. |
| `game/Q3_Interface.cpp:4207` | Q3_SetEvent | `if ( ent->s.number != 0 )` | story | none | Script target is the entity named `Munro` = P1 only. P2 must never get that targetname. |
| `game/Q3_Interface.cpp:5506` | Q3_SetFormation | `G_CreateFormation(&g_entities[0]);` | story | none |  |
| `game/Q3_Interface.cpp:6882` | Q3_Set | `G_TeamRetaliation( NULL, &g_entities[0], qfalse );` | story | none |  |
| `game/Q3_Interface.cpp:7564` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7570` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7576` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7582` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7587` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7592` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7597` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7602` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7607` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7612` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7617` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7622` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7627` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7632` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7637` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7642` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7647` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7652` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7657` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7662` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7667` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7672` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7677` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7682` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7687` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7692` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7697` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7702` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7707` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7712` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7717` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:7722` | Q3_GetFloat | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/Q3_Interface.cpp:8964` | Interface_Init | `client = &level.clients[0];` | story | none | Objective get/reset (Q3_GetFloat / Interface_Init) on P1's session. |
| `game/bg_public.h:57` |  | `#define CS_MAX (CS_PLAYERS+MAX_CLIENTS)` | leave | none |  |
| `game/g_ICARUS.cpp:123` | ICARUS_Shutdown | `gentity_t *ent = &g_entities[0];;` | leave | none | Loop start. |
| `game/g_active.cpp:478` | G_NPCMunroMatchPlayerWeapon | `if ( g_entities[0].client->ps.weapon == WP_PHASER \|\| g_entities[0].client->ps.weapon > ...` | story | none | G_NPCMunroMatchPlayerWeapon (holodeck double copies P1). |
| `game/g_active.cpp:484` | G_NPCMunroMatchPlayerWeapon | `newWeap = g_entities[0].client->ps.weapon;` | story | none | G_NPCMunroMatchPlayerWeapon (holodeck double copies P1). |
| `game/g_ambients.cpp:528` | flier_find_enemy | `//g_entities[0]` | dead | none | Commented out / `#if 0`. |
| `game/g_ambients.cpp:622` | flier_fire_children | `//curCheck->enemy = &g_entities[0]; // always target the player - this might be bad, th...` | dead | none | Commented out / `#if 0`. |
| `game/g_client.cpp:849` | ClientSpawn | `client->ps.clientNum = index;` | leave | none | NPCs (and P2) get `ps.clientNum = s.number` (NPC_spawn.cpp:502). |
| `game/g_combat.cpp:417` | G_MakeTeamVulnerable | `gentity_t *ent = &g_entities[0];` | story | none |  |
| `game/g_combat.cpp:418` | G_MakeTeamVulnerable | `gentity_t *self = &g_entities[0];` | story | none |  |
| `game/g_combat.cpp:507` | player_die | `if (( &g_entities[0] != NULL && g_entities[0].client ) && (self->s.number != 0))` | story | none | PERS_TEAMMATES_KILLED counted on P1. |
| `game/g_combat.cpp:509` | player_die | `g_entities[0].client->ps.persistant[PERS_TEAMMATES_KILLED]++;` | story | none | PERS_TEAMMATES_KILLED counted on P1. |
| `game/g_combat.cpp:1107` | G_FriendlyFireReaction | `if ( !targ->NPC ) return;` | leave | none | P1 shooting P2 is not NPC friendly fire. |
| `game/g_combat.cpp:1469` | G_Damage | `if ( targ->s.number == 0 && targ != attacker )` | dead | none | Commented out / `#if 0`. |
| `game/g_main.cpp:21` |  | `gentity_t *playerEnt = &g_entities[0];` | leave | none | `playerEnt` is initialised before `g_entities` is allocated, so it is NULL; unused. Do not start using it. |
| `game/g_main.cpp:240` | InitGame | `g_entities = (struct gentity_s *) G_Alloc( MAX_GENTITIES * sizeof(g_entities[0]) );` | leave | none | Allocation / loop start. |
| `game/g_main.cpp:241` | InitGame | `memset( g_entities, 0, MAX_GENTITIES * sizeof(g_entities[0]) );` | leave | none | Allocation / loop start. |
| `game/g_main.cpp:246` | InitGame | `level.clients = (struct gclient_s *) G_Alloc( level.maxclients * sizeof(level.clients[0...` | leave | none | Allocation / loop start. |
| `game/g_main.cpp:249` | InitGame | `g_entities[0].client = level.clients;` | leave | none | Allocation / loop start. |
| `game/g_main.cpp:254` | InitGame | `globals.num_entities = MAX_CLIENTS;` | leave | none | Allocation / loop start. |
| `game/g_main.cpp:844` | G_RunFrame | `for ( i = 0, ent = &g_entities[0]; i < globals.num_entities ; i++, ent++)` | leave | none | Allocation / loop start. |
| `game/g_main.cpp:861` | G_RunFrame | `for ( i = 0, ent = &g_entities[0]; i < globals.num_entities ; i++, ent++)` | leave | none | Allocation / loop start. |
| `game/g_mover.cpp:748` | Use_BinaryMover | `else if ( g_entities[0].client->tourSess.tour_objectives[OBJ_SECURITYCODE].status != OB...` | story | none | Tour objective. |
| `game/g_mover.cpp:749` | Use_BinaryMover | `g_entities[0].client->tourSess.tour_objectives[OBJ_SECURITYCODE].status != SET_OBJ_SUCC...` | story | none | Tour objective. |
| `game/g_nav.cpp:1289` | Svcmd_Nav_f | `NAVDEBUG_curGoal = navigator.GetNearestNode( &g_entities[0], g_entities[0].waypoint, NF...` | story | none | Debug console / debug draw. |
| `game/g_nav.cpp:1427` | NAV_ShowDebugInfo | `int nearestNode = navigator.GetNearestNode( &g_entities[0], g_entities[0].waypoint, NF_...` | story | none | Debug console / debug draw. |
| `game/g_nav.cpp:1430` | NAV_ShowDebugInfo | `nearestNode = NAV_TestBestNode( g_entities[0].currentOrigin, nearestNode, testNode );` | story | none | Debug console / debug draw. |
| `game/g_objectives.cpp:79` | OBJ_SaveObjectiveData | `client = &level.clients[0];` | story | none |  |
| `game/g_objectives.cpp:115` | OBJ_LoadObjectiveData | `client = &level.clients[0];` | story | none |  |
| `game/g_ref.cpp:39` | TAG_ShowTags | `if ( gi.inPVS( g_entities[0].currentOrigin, (*rti)->origin ) )` | leave | none | Debug drawing. |
| `game/g_ref.cpp:416` | TAG_FlushPending | `for ( int i = MAX_CLIENTS; i < globals.num_entities; i++ )` | leave | none | Loop from MAX_CLIENTS (=1) includes P2; it only touches ref tags. |
| `game/g_squad.cpp:754` | NPC_RecieveCommand | `next = &g_entities[0];` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:872` | Svcmd_Comm_f | `next = &g_entities[0];` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:879` | Svcmd_Comm_f | `NPC_RecieveCommand(&g_entities[0], next, command, gi.argv(2));` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:924` | Svcmd_Hail_f | `next = &g_entities[0];` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:928` | Svcmd_Hail_f | `NPC_HailSquadMate(&g_entities[0], next);` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:934` | Svcmd_Hail_f | `next = &g_entities[0];` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:940` | Svcmd_Hail_f | `NPC_HailSquadMate(&g_entities[0], next);` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:950` | Svcmd_Form_f | `G_CreateFormation(&g_entities[0]);` | story | none | Squad commands come from the host console/keys. |
| `game/g_squad.cpp:965` | target_create_formation_use | `G_CreateFormation(&g_entities[0]);` | story | none | Squad commands come from the host console/keys. |
| `game/g_svcmds.cpp:168` | ConsoleCommand | `g_entities[0].activeBoltOn = G_AddBoltOn( &g_entities[0], cmd3 );` | story | none | Host console commands. |
| `game/g_svcmds.cpp:173` | ConsoleCommand | `G_RemoveBoltOn( &g_entities[0], cmd3 );` | story | none | Host console commands. |
| `game/g_svcmds.cpp:180` | ConsoleCommand | `Q3_SetActiveBoltOnStartFrame( 0, g_entities[0].client->renderInfo.boltOns[g_entities[0]...` | story | none | Host console commands. |
| `game/g_svcmds.cpp:212` | ConsoleCommand | `ICARUS_RunScript( &g_entities[0], va( "%s/%s", Q3_SCRIPT_DIR, cmd2 ) );` | story | none | Host console commands. |
| `game/g_svcmds.cpp:253` | ConsoleCommand | `g_entities[0].client->playerTeam = team;` | story | none | Host console commands. |
| `game/g_trigger.cpp:156` | Touch_Multi | `if ( other->NPC == NULL ) return; //NPCONLY` | leave | none | P2 has no NPC struct, so it is treated as a player: correct. |
| `game/g_trigger.cpp:278` | trigger_cleared_fire | `"wait" : Seconds between triggerings, 0.5 default, number<=0 means one time only.` | dead | none | Commented out / `#if 0`. |
| `game/g_trigger.cpp:559` | trigger_push_touch | `if ( other->NPC == NULL ) return; //NPCONLY` | leave | none | Same. |
| `game/g_utils.cpp:430` | G_InitGentity | `The slots from 0 to MAX_CLIENTS-1 are always reserved for clients, and will` | dead | none | Commented out / `#if 0`. |
| `game/g_utils.cpp:449` | G_Spawn | `e = &g_entities[MAX_CLIENTS];` | leave | none | G_Spawn starts after the client slots; P2 gets an ordinary slot like an NPC. |
| `game/g_utils.cpp:450` | G_Spawn | `for ( i = MAX_CLIENTS ; i<globals.num_entities ; i++, e++) {` | leave | none | G_Spawn starts after the client slots; P2 gets an ordinary slot like an NPC. |
| `game/g_utils.cpp:801` | Svcmd_Use_f | `G_UseTargets2( &g_entities[0], &g_entities[0], cmd1 );` | story | none | `use` console command. |
| `game/g_utils.cpp:946` | G_ChangeMap | `if (g_entities[0].client->ps.pm_type == PM_DEAD)` | story | none | No map change while P1 is dead. |
| `game/g_weapon.cpp:78` | W_CheckBorgAdapt | `if ( g_entities[0].client )` | story | none | Borg adaptation counter (shared, see Q3_Interface.cpp:1215). |
| `game/g_weapon.cpp:80` | W_CheckBorgAdapt | `int *borgAdaptHits = g_entities[0].client->ps.borgAdaptHits;` | story | none | Borg adaptation counter (shared, see Q3_Interface.cpp:1215). |
| `game/g_weapon.cpp:2905` | CalcMuzzlePoint | `if((ent->client->ps.weapon != WP_BORG_ASSIMILATOR) && (!lead_in)) //&& ent->s.number != 0` | leave | none | Comment only. |
| `game/q_shared.h:880` | crandom | `#define MAX_CLIENTS 1 // 128 // absolute limit` | leave | none | MAX_CLIENTS stays 1 (proposal decision). |
| `game/q_shared.h:981` | crandom | `int clientNum; // ranges from 0 to MAX_CLIENTS-1` | leave | none | MAX_CLIENTS stays 1 (proposal decision). |
| `game/q_shared.h:1098` | crandom | `int clientNum; // 0 to (MAX_CLIENTS - 1), for players and corpses` | leave | none | MAX_CLIENTS stays 1 (proposal decision). |
| `sp/bink_ff.c:48` | S_Update | `#ifdef MAX_CLIENTS` | leave | none | Bink stream id, not a player index. |
| `sp/bink_ff.c:49` | S_Update | `#define BINK_STREAM ( MAX_CLIENTS * 2 + 1 )` | leave | none | Bink stream id, not a player index. |
| `sp/sp_bridge.cpp:717` | SP_SpawnServer | `sv_maxclients\1 (also 1355)` | leave | none | MAX_CLIENTS stays 1. |

