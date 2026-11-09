# Co-op M1, steps 3 to 6: the replica guest (design)

Status: design only. Steps 1 and 2 (the connection, and the guest's input steering player 2) are
in: `efcode/sys/sys_coop_net.c`, `efgame/src/coop/coop_net.cpp`. This note plans the rest of M1
against the code as it is now. Background: `docs/coop-proposal.md` (the host runs the game, the
guest mirrors it).

## Where we are
- The guest connects (`coop_connect`), sends its newest commands, and P2 on the host moves with
  them. Until the replica exists, the guest loads the host's map on its own (`coop_guestLoadMap`)
  and plays a private copy of it, so what it sees is not the host's world.
- The connection is unreliable datagrams: a status infostring once a second, input, keepalives and
  disconnects. Nothing in it is reliable or fragmented yet. Both are needed below.

## Step 3: the replica guest

### Replica mode
The guest loads the same map and the same game module, with the latched cvar `sp_coop 2`
("replica"; 1 stays "host"). In replica mode:
- The bridge (`sp_bridge.cpp`) runs no `ge->RunFrame` and no `ge->ClientThink` for P1. ICARUS,
  AI, movers and triggers therefore never run on the guest.
- `InitGame` still runs (it registers models, sounds and configstrings, and sets up `level`), but
  its spawned entities are thrown away by the first full state from the host (below), so the guest's
  entity numbering never has to match a spawn of its own.
- The cgame's one piece of game logic, the `G_UseTargets` call when a camera track ends
  (`cg_camera.cpp`), is skipped on the guest (`COOP_IsReplica()`).
- Saving is refused on the guest (only the host saves).

### Full state: the in-memory save
A join (and later any resync: a host load, a level change, a guest that fell behind) sends the
whole level the way a save stores it. The save code already turns every pointer into an index
(`EnumerateFields` with the `savefields_*` tables in `g_savegame.cpp`), covers entities, clients,
NPC structs, `level`, ICARUS and the portal state, and the bridge already loads such a state
(`SP_FinishTransition` with a save). So:
1. Host: write the level into memory instead of a file: the bridge's `I_Append`
   (`gi.AppendToSaveGame`) gets a memory sink for the length of one `WriteLevel` call. P2 is
   handled as for a normal save (`COOP_PreSave`), then written as one extra chunk the guest knows
   (it isn't in a `.sav` at all).
2. Compress it with the engine's zlib (`efcode/zlib`, already linked), and send it in fragments of
   about 1200 bytes, each numbered; the guest acks the fragments it has; the host resends the rest.
   A typical level should be some hundreds of KB raw and much less compressed.
3. Guest: run the existing load path on the buffer (`ReadLevel` through `gi.ReadFromSaveGame`
   redirected to the buffer), then add P2's chunk.

### Changes each tick: entity deltas
Every game frame the host makes a "replica frame" for the guest:
- Per entity in use: a copy of `gentity_t` with its pointers turned into indexes by the same field
  tables (into a scratch copy, exactly as the save does), plus its `gclient_t` and `gNPC_t` when it
  has them.
- Compared with the copy the guest last acknowledged (kept per entity on the host, a ring of the
  last 32 frames as in a Quake server's client frames), as runs of changed bytes:
  `entity number, struct, [offset, length, bytes]...`; new and freed entities are marked.
- The packet is zlib-compressed and carries the frame number and the frame it is a delta from. The
  guest acks frame numbers in its input packets. A lost packet costs nothing: the next frame is a
  delta from the last acked one. If the guest falls more than 32 frames behind, the host sends a
  full state.
- Byte-runs over whole structs (rather than chosen fields) need no audit of what the cgame reads,
  and the save tables already know which members are pointers. Fields that change every frame
  without mattering (think times relative to `level.time`, as `g_regress.cpp` treats them) can be
  masked if measurement says they cost too much.
- `level` (time, frame number), `player_locked`, `in_camera` and the objectives go with every frame
  (small).

The guest writes each delta into its entities, turns indexes back into pointers (the load code's
`EnumerateFields` reverse), relinks moved entities in the bridge's world (`gi.linkentity`), and the
bridge builds its snapshot from them as now, with P2's `playerState_t` instead of P1's.

### Reliable messages
Configstrings, server commands and fragments need delivery in order. A small reliable layer in
`sys_coop_net.c`, the Quake 3 way: each side numbers its reliable messages, every packet carries
the unacknowledged ones and the number of the last one received; resend until acked.

## Step 4: the guest's cgame is P2
- `COOP_ViewClientNum()` returns P2's entity number on the guest (0 on the host). The cgame's
  hardcoded "entity 0 is me" uses (about 16 `g_entities[0]` plus some `cg_entities[0]`, and
  `cg.snap->ps.clientNum`) change to it: HUD, view weapon, crosshair, view angles, the player's
  own model in first person.
- The bridge on the guest builds `g_snap.ps` from P2's client and stamps `cmdNum` with the guest's
  own command number, so the cgame's prediction replays the guest's unacknowledged commands on top
  of the host's P2 state. Prediction needs no game entities (`cg_pmove.gent = NULL`), and traces go
  through the replica's world.
- The host keeps acknowledging the newest guest command it applied (its sequence number) in each
  replica frame, so the guest's prediction knows what to replay.

## Step 5: sounds, prints, subtitles
- The bridge's `I_SendServerCmd` and `I_SetCS` on the host also queue the command or configstring
  as a reliable co-op message; the guest feeds them to its cgame as its own bridge does locally
  (centre prints, subtitles, voice lines, music, objectives).
- Sounds started by the game as entity events travel with the entity deltas (the cgame plays them
  from the snapshot). Sounds the game starts directly through the engine (`gi.S_Override`, voice
  lines) need an explicit message.

## Step 6: measure
- Bytes per second on borg1 (busy), voy1 (many NPCs) and a quiet map; CPU of the host's per-frame
  serialization and the guest's apply. Two copies on one Mac, then two Macs on a LAN.
- `coop_status` prints the numbers; a `coop_netgraph`-like line is enough.

## Order and what can start alone
1. The reliable layer and fragmentation in `sys_coop_net.c` (engine only; testable with a probe).
2. The in-memory save on the host and the load from memory on the guest (bridge + `g_savegame`
   hooks): testable by writing and reading back on one machine.
3. Replica mode (`sp_coop 2`) in the bridge: no game frames, snapshot from P2.
4. Deltas, then the cgame "local client", then forwarded commands.
Steps 1 and 2 are cleanly separable and come first.

## Open questions
- `.pk3` match: today every loaded `.pk3` must match. A guest with extra Holomatch map packs in
  `baseEF` is refused; maybe only the packs the SP game reads should count.
- A host that pauses (menu, console) stops its game frames: the guest should see "paused".
- The host's game pausing when its window loses focus matters for two copies on one Mac.
