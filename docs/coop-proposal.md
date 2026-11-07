# Co-op for the single-player campaign — proposal

Status: proposal only, nothing implemented.

Goal: player 1 hosts, player 2 joins. Player 1 is the story player and goes through the
cutscenes. Player 2 always spawns next to player 1 on the same map. Either player can trigger
game events. Co-op fixes for edge cases must not affect single player, Holomatch or other modes.
Saves keep player 1's state and load back in single player.

## How single player works today

These facts decide most of the design:

1. **There's no server or network path in SP.** `EFAndroid-SP/app/jni/sp/sp_bridge.cpp` runs the
   SP game module inside the engine with its own small in-process world, and builds the one
   snapshot itself from entity 0 (`BuildSnapshot`).
2. **The game allows exactly one client.** `MAX_CLIENTS` is `1` (`efgame/src/game/q_shared.h`).
   Entity 0 is the player and NPCs start at 1. Save files dump entities by number.
3. **The cgame reads and writes game entities directly.** It isn't a networked client:
   `cg_main.cpp` binds `cg_entities[i].gent = &g_entities[i]`, and there are about 515 `->gent`
   uses. It even runs game logic itself: the camera fires `G_UseTargets` when a camera track ends
   (`cg_camera.cpp`).
4. **NPCs are already full clients.** Each one gets its own client struct from `G_Alloc`
   (`NPC_spawn.cpp`) and moves through the same `ClientThink` as the player (`NPC.cpp`). That's
   the opening for player 2.
5. **"The player" is hardcoded as entity 0 in many places.** Game code has about 99
   `g_entities[0]` uses, about 71 `number == 0` checks and about 371 `player` references, across
   28 files. The cgame adds about 16 `g_entities[0]` uses plus several `cg_entities[0]` ones.

## Recommended architecture: the host runs the game, the guest mirrors it

**Host (P1):** unchanged single player. P1 stays entity 0 and `MAX_CLIENTS` stays 1, so entity
numbering and the save format don't change. Player 2 is one extra entity, built like an NPC, with
a client struct, no NPC brain and a `coop player` flag. Its input comes from the network and goes
through `ClientThink`, the same path NPCs use. Only the host runs game logic, scripts and AI.

**Guest (P2):** loads the same game module and map but runs it in a "replica" mode: no game
frames, no scripts. Each tick the host sends entity changes, and the guest writes them into its
own copy of the entities. The SP cgame then works as usual against real data. The guest builds its
own snapshot with P2's player state, and the cgame predicts P2's movement locally (that prediction
doesn't depend on game entities: `cg_pmove.gent = NULL` in `cg_predict.cpp`). This is the classic
Quake client/server model.

**Why not the alternatives:**
- **Lockstep** (both machines run the game, swap only inputs) needs the two simulations to stay
  identical. They wouldn't, because each machine's cgame writes into game state from its own point
  of view, and because of Raven-era undefined behaviour.
- **Rendering with the multiplayer cgame** loses SP-only effects, NPC animation and cutscenes.

**Transport:** a separate co-op protocol on its own port, built on the engine's existing
NET/Netchan code. The Holomatch protocol isn't touched. The guest is refused unless the build
version and `.pk3` checksums match.

## Work areas

### 1. Keeping co-op isolated (first)
- A latched `sp_coop` cvar; the code lives in new `coop_*.cpp` files on the game side and the
  bridge side.
- Existing code only gets one-line hooks: `if (COOP_Active()) COOP_OnX(...)`. With co-op off,
  every hook does nothing, so SP and Holomatch behave as they do now.
- macOS only. The co-op files are only in the macOS build, and the Android build isn't touched.

### 2. Player 2's entity
- Spawns at a clear spot next to P1, found with traces, on new game, load, map change and respawn.
- If P2 dies, they respawn next to P1 after a delay. If P1 dies, it's the existing game-over or
  reload.
- P2 is non-solid and hidden while a cutscene runs.

### 3. "Who is the player" audit (bulk of the gameplay work)
Each of the roughly 500 sites is sorted into one group and swapped for a helper:
- **Story player = P1:** the camera, the script target `player`, objectives, level changes, the
  squad following you. `COOP_StoryPlayer()`.
- **Any player:** triggers, the use key, pickups, damage. `COOP_IsPlayer(ent)`.
- **Nearest player:** enemies choosing a target, reactions. `COOP_NearestPlayer(org)`.

If P2 trips a story trigger, the script's activator can be remapped to P1 so story scripts still
see "the player".

### 4. Cutscenes
- On the host, scripts drive the cgame camera through direct `CGCam_*` calls. Those calls are
  mirrored to the guest as reliable messages.
- `player_locked` and `in_camera` also apply to P2.
- The `G_UseTargets` call inside the cgame camera code runs on the host only and is turned off on
  the guest.

### 5. Mirroring the world to the guest
- Entities are serialized with the field tables the save code already uses (pointers become
  indexes). Each entity is compared with the last copy the guest confirmed, and the changes are
  compressed.
- Game globals the cgame reads are sent too: `player_locked`, objectives, crosshair state and
  similar.
- Configstrings and server commands are forwarded: centre-print, subtitles, cinematics.
- A full resync reuses the in-memory save. That covers joining mid-game and recovering if the
  guest falls out of step.

### 6. Guest cgame
Every hardcoded "entity 0 is me" in the cgame (HUD, view weapon, crosshair, scoreboard, view
angles) changes to "the local client": 0 on the host, P2's number on the guest.

### 7. Saves
- Only the host can save. P2 is unlinked or skipped while the save is written, so the `.sav` is a
  normal SP save that loads in single player as usual.
- An optional `name.coop` file next to it holds P2's health, armour and weapons. SP ignores it.
- When the host loads a save, the guest is resynced. Autosaves (level start, death respawn point)
  work the same way.

### 8. Launcher
**Host Co-op** (with **Copy Invite**) and **Join Co-op**, reusing the existing automatic router
port opening. See "Internet play" below.

### 9. Edge-case fix layer
- `coop_fixes.cpp`, with handlers keyed by map name plus a targetname or script, run at fixed
  points: entity spawn, trigger fired, script started, camera start or end, map change.
- Optionally per-map data files such as `coop/<map>.cfg` inside a `.pk3`, so a fix like "teleport
  P2 to P1 when trigger X fires" or "P2 can't use Y" doesn't need a rebuild.
- A general safety net: if P2 is too far from P1, or out of their line of sight for N seconds
  (stuck behind a one-way door or lift), teleport P2 to P1.
- Expected trouble spots: turbolifts and transporter sequences, turret and stealth sections, Borg
  assimilation, scripts that assume the player is alone.

## Risks
- **Mirroring cost.** Full world state is roughly hundreds of KB. As compressed changes it should
  be fine on a LAN; over the internet it needs measuring.
- **Code that assumes only one client exists.** The cgame and scripts touch entities in many
  places; some of these only show up in a playthrough.
- **Resync hitches** when the host loads a save or changes level.

## Phases
- **M0, no networking.** Co-op flag and P2 as a local puppet entity (console-controlled or a simple
  "follow P1" input). Do the player audit, cutscene hiding and save exclusion against it.
- **M1.** Co-op protocol plus the replica guest: it can watch and move around a live map.
- **M2.** Cutscene mirroring, map changes, saves and resync.
- **M3.** Map-by-map co-op playthrough, filling in the fix layer.

## Decisions
- **Cutscenes:** P2 watches the same camera as P1.
- **Carry-over:** P2 keeps their weapons and stats across maps through the `.coop` file.
- **Internet:** direct connection to the host, no server of our own (see below). LAN first.
- **Platform:** macOS only. The Android build isn't touched.

## Inventory: what happens when one player picks up a weapon

Today, in `Touch_Item` (`g_items.cpp`):
- Items without a team or monster spawnflag can only be picked up by entity 0. That check becomes
  `COOP_IsPlayer`, or P2 can't pick anything up.
- Items never respawn in SP. The first player to touch one takes it and it's gone.
- A pickup fires the item's targets with the picker as activator, so picking up an item can start
  a story event.
- Scripts give weapons with `set weapon` (`Q3_SetWeapon` in `Q3_Interface.cpp`). On the player
  that also calls `CG_ChangeWeapon` directly, which only reaches the host's cgame.

Will it hold up the game? Mostly no. Progress is driven by triggers and scripts, and no script
command reads the player's inventory. The real risk is a fight that needs a weapon only the other
player has, like the I-Mod against Borg that adapt to the phaser.

Rule:
- **Weapons and holdables are shared.** When either player picks one up, both get it. The item
  vanishes as usual and its targets fire once, with P1 as activator.
- **Script-given weapons go to both** (`set weapon` on `player`), with the weapon-switch call
  mirrored to the guest.
- **Ammo, health and armour are first come, first served**, as in other co-op games.
- If ammo turns out to be too scarce with two players, either player can share an ammo pickup
  with the other (a fix-layer option).

## Internet play: no server needed

The host's Mac is the server. The guest connects straight to it, as with Holomatch:
- The launcher already opens the router port automatically (NAT-PMP / UPnP) for hosting. Co-op
  reuses that for its own port.
- The host gets a **Copy Invite** link (address and port, same idea as the existing Join links),
  sends it to their friend, and the friend opens it or types the address under **Join Co-op**.
- Co-op games aren't put on the Elite Force master servers. Those list Holomatch servers, and a
  co-op game is a private session between two people.

A server of our own would only be needed when neither side can open a port: carrier-grade NAT
(common on mobile hotspots and some ISPs) or a router without NAT-PMP/UPnP. That needs a meeting
point for NAT hole punching, and a relay when that fails too. That's real infrastructure to run,
so it's left out. A failed port opening shows a clear message instead.

The README says single player never opens a network connection. With co-op off that stays true;
the README needs a line saying co-op opens one.
