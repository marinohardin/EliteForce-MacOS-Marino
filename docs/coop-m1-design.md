# Co-op M1, steps 3 to 6: the replica guest (design)

Status: steps 1 to 3 are in (2026-11-10). Step 3 was built differently from the plan below it in
places; "Step 3 as built" says how. Code: `efcode/sys/sys_coop_net.c` (connection, frames),
`sp/sp_bridge.cpp` (host frames, the guest's replica level), `efgame/src/coop/coop_net.cpp`
(input), `efgame/src/coop/coop_replica.cpp` (entity images). Background: `docs/coop-proposal.md`
(the host runs the game, the guest mirrors it).

## Where we are
- The guest connects (`coop_connect`), sends its newest commands, and P2 on the host moves with
  them. While a guest is connected and its input is missing or stale, P2 holds still; P2 follows
  P1 only with no guest.
- Only the `.pk3` files single player reads must match (the core packs, packs with SP data
  `ext_data/` or `.ibi`, and packs overriding a core pack's SP map); Holomatch map packs may differ.
- The guest loads the host's map as a replica (`sp_coop 2`) and shows the host's world.

## Step 3 as built
- No reliable layer, no full state from `WriteLevel`. The host's world is `COOP_NUM_SLOTS` slots:
  each entity (0..1023), each configstring (1024..2047), the level's globals (2048) and the newest
  server commands (2049, the last 32 with their sequence numbers; the guest runs the ones newer
  than it has seen). Every frame the host sends the slots that differ from the newest frame the
  guest acked; a guest with no ack gets every slot (a "whole" frame, which is just a change from
  nothing), at most once a second while it stays unacked. Losing a fragment loses that frame only.
- Each entity slot is the save's own images (`GENT`, `GNPC`, `GCLI`, `PARM` chunks made by
  `EnumerateFields`, pointers as indexes, strings inline) written to memory by swapping
  `gi.AppendToSaveGame` for a memory sink; the guest reads them with `EvaluateFields` the same way.
  No ICARUS state goes: the guest runs no scripts. P2 needs no extra chunk, it is an entity.
- Frames are compressed with the system's Compression library (zlib; `efcode/zlib` has only
  inflate), cut into 1200-byte fragments; 32 frames are kept on both sides. Acks ride on input and
  keepalive packets; the guest sends one at once for every new frame.
- A level id counts the host's level starts; frames of a level the guest hasn't loaded make it load
  that map with `sp_coop 2`. The replica level runs no game frames: the bridge applies the host's
  frames (relinking entities, brush model clips from `s.modelindex`), sets the configstrings, and
  builds a snapshot per host frame with P2's player state. The cgame draws one frame behind, moving
  on with real time between frames.
- The guest keeps its own media handles (model, skin, sound and animation file indexes) and
  registers models again when an entity's model names change. Camera mirroring (`in_camera`) is
  left for M2.
- First numbers (borg2, host idle, two copies on one Mac): a whole frame is 44 KB compressed;
  steady state about 22 to 26 KB/s at 20 frames/s; the host spends 0.7 ms building and 0.25 ms
  sending per frame, the guest 0.05 ms decoding.

## Step 3: the plan it started from

### Replica mode
The guest loads the same map and the same game module, with the latched cvar `sp_coop 2`
("replica"; 1 stays "host"). In replica mode:
- The bridge (`sp_bridge.cpp`) runs no `ge->RunFrame` and no `ge->ClientThink` for P1. ICARUS,
  AI, movers and triggers therefore never run on the guest.
- `InitGame` still runs (it registers models, sounds and configstrings, and sets up `level`), but
  its spawned entities are overwritten by the host's (below), so the guest's entity numbering
  never has to match a spawn of its own.
- The cgame's one piece of game logic, the `G_UseTargets` call when a camera track ends
  (`cg_camera.cpp`), is skipped on the guest (`COOP_IsReplica()`).
- Saving is refused on the guest (only the host saves).

### Full state (planned: an in-memory `WriteLevel`; built: a change from nothing, above)
The save code already turns every pointer into an index (`EnumerateFields` with the `savefields_*`
tables in `g_savegame.cpp`) and covers entities, clients, NPC structs and `level`. The plan was to
write a whole `WriteLevel` to memory, send it reliably in acked fragments and load it with
`ReadLevel`. Sending per-entity images as slots does the same with one mechanism.

### Changes each tick (planned: byte runs per struct; built: whole slot images)
Byte runs inside each struct were planned; whole images of the slots that changed, compressed
together, turned out small enough (one moving entity costs its images, about 1 to 3 KB raw, a few
hundred bytes compressed). The ring of 32 acked frames is as planned.

### Reliable messages (not built)
Configstrings and server commands travel as slots instead, so they need no reliable layer.

## Step 4: the guest's cgame is P2 (built)
- `COOP_ViewClientNum()` is P2's entity number in a replica (0 elsewhere). The cgame's "entity 0 is
  me" uses that matter for the view change to it: the HUD's player (crosshair names, radar,
  teleport effect, assimilation overlay), the eye point and weapon changes. The rest already went
  through `cg.snap->ps.clientNum`, which is P2's in the guest's snapshots. The scoreboard's mission
  statistics and the camera code still read entity 0 (P1, the story player).
- The guest makes one command for every host frame it takes in (the latest angles and moves, with
  every button pressed since the last one), numbers it, keeps it, and sends it at once with the few
  before it. The host queues them (at most 3; a full queue drops the oldest but keeps its buttons)
  and applies one per game frame; with none waiting P2 stands with the newest angles. Each replica
  frame names the last command applied (`ackCmd`).
- The guest's snapshot stamps `cmdNum` with that ack and `ps.commandTime` with ack * 50 ms; command
  n has `serverTime` n * 50 ms, and the one being made now n * 50 + the time since the last one. So
  the cgame's own prediction (`CG_PredictPlayerState`, used in a replica even at timescale 1)
  replays every unacknowledged command as one host frame, the way the host's `ClientThink` runs it.
  The prediction-error decay is skipped in a replica (the newest command stays current over several
  drawn frames, so the usual check would see its own partial move as an error).
- Measured on borg2 with two copies on one Mac: the host's P2 matched the guest's prediction of
  each acknowledged command exactly (0.00 units) in 405 of 406 frames (one 14-unit miss right after
  joining). The guest's commands run about 2 host frames behind; prediction hides it.

## Step 4: the plan it started from
- `COOP_ViewClientNum()` returns P2's entity number on the guest (0 on the host). The cgame's
  hardcoded "entity 0 is me" uses change to it.
- The bridge on the guest builds `g_snap.ps` from P2's client and stamps `cmdNum` with the guest's
  own command number, so the cgame's prediction replays the guest's unacknowledged commands on top
  of the host's P2 state. Prediction needs no game entities (`cg_pmove.gent = NULL`), and traces go
  through the replica's world.

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
- A host that pauses (menu, console) stops its game frames: the guest should see "paused".
- The host's game pausing when its window loses focus matters for two copies on one Mac.
