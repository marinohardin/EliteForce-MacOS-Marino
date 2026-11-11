# Co-op M2: cutscenes, level changes, saves (as built)

Status: steps 7, 8, 9, 9a (lag compensation for player 2's shots), 9b (the guest's own fire effects at
once) and 10 (internet: router port, invite, bandwidth trimming) are in (2026-11-10, branch
`1_4_0/coop-m1`). Background: `docs/coop-proposal.md` (the host runs the game,
the guest mirrors it) and `docs/coop-m1-design.md` (the replica: slots, acknowledged changes, prediction).

## Cutscenes (step 7)

### The camera: state, not calls
The plan was to send each `CGCam_*` call to the guest as a reliable message. Built instead: the camera's
whole state goes with the level's globals (an `RCAM` chunk in the globals slot) while a camera runs, or
while its letterbox bars or a fade are still moving after it. Why state:
- `camera_t` is plain data (the save code writes it whole), about 400 bytes, and the replica already
  sends the globals slot every host frame; a separate reliable channel for calls would be new machinery.
- Every timed part of a shot (move, pan, zoom, fade, bars, shake) is a start value, an end value, a
  duration and a start time on the level clock. The guest's cgame runs `CGCam_Update` on the host's state
  with its own clock, which follows the host's level time, so between two host frames it shows the same
  shot, not a stepped copy.
- Following a camera group and tracking path corners advance per drawn frame; each host frame replaces
  the guest's copy, so they can't drift.
- A guest that joins (or loses frames) mid-cutscene sees the current shot at once; calls would need
  a replay of everything since the camera started.

`in_camera` is the host's on the guest, so the guest's cgame draws the camera view, hides the HUD and
view weapon, and culls its snapshot by the camera's position (`CG_GetCameraPos`), as the host does. The
bars are in screen pixels, so the host sends its view height and the guest scales them to its own. The
colours scripts give scroll, caption and centre text (`textcolor_*`, set only by script commands) go with
the globals: on the guest they were zero, which made the borg1 opening crawl invisible there.

The one piece of game logic in the camera code, a track's end firing its path corner's targets
(`G_UseTargets` in `CGCam_TrackEntUpdate`), runs on the host only.

### Player 2 during a cutscene
As in M0: hidden, not solid, not hurt, not targeted and not moving while a camera runs; `player_locked`
stops its movement too. The guest's own input still goes to the host and is dropped there. When the camera
ends, player 2 rejoins player 1 unless it is close with a clear way; it looks again half a second later,
because scripts often put player 1 in place just after the camera (borg1 left player 2 at the opening spot
while a guest steered it, since a steered player 2 doesn't follow).

### Skipping
The host's Use Item in a cutscene (`skippingCinematic`, timescale 100) skips for both: the host's game runs
many frames per drawn frame and the guest gets the last of each drawn frame's (one replica frame per
drawn frame while skipping, not one per game frame). The guest mirrors `skippingCinematic`, draws the skip
screen with "Host is skipping the cutscene", and stops the game's sounds as the host does. The guest can't
skip; its Use Item goes to a frozen player 2.

### In-game movies: played on the guest (not just "Host paused")
Chosen: the guest plays the same movie. A movie is the story (borg1's ending, voy1's opening flyby and
the captain's log), so showing "Host paused" over it would make the guest miss it. Playing it keeps both
in step at almost no cost (both copies have the files; nothing streams) and without a new mechanism:
- The host notes each `inGameCinematic` the game starts (`I_SendConsole`) and every frame's commands slot
  ends with the newest one (number and name) and whether a movie is playing on the host right now.
- The frame that carries a new movie is sent in the game frame that started it, before the host's game
  pauses for it, so the guest starts it at the same moment (both use the same `inGameCinematic` path,
  which pauses the guest's sounds too).
- The end follows the host: when the host's game goes on after pausing for the movie (it ended, or the host
  skipped it), the guest stops its own. If the guest's copy ends first, or the guest skips it, the guest
  sees "Host paused" until the host's ends. A guest that arrives while the host's movie plays (it loaded
  the level during it) starts it then and is stopped with the host's.

## Level changes and loads (step 8)

Following a map change already worked in M1 (frames of a new level make the guest load that map with
`sp_coop 2`, and a new level gets a whole frame). What was missing is that the host went on alone while the
guest loaded: the guest arrived seconds late and missed the start of the level (opening camera, crawl,
first voice line).

Now a level that starts on the host with a guest connected (New Game, a map change, a load, the death
respawn's autosave) holds its first game frame until the guest has the level up: the host draws the
frozen frame with "Waiting for player 2", its level time stands still and its sounds are held, and it
sends a frame every 250 ms (the first tells the guest which map to load). The guest says which host level
it has running (with its cgame up) at the end of every ack (co-op protocol 5); the hold ends when that is
the new level, when the guest leaves, or after 30 s. Measured on one Mac: borg1 1.1 to 1.6 s, voy1 1.1 to
1.2 s, borg2 after a map change 1.0 s, a borg2 load 1.1 s.

Frames of a held level carry a flag; the guest then shows all of that level's server commands so far
instead of only new ones (borg1's crawl is sent in the settle frames, before the hold). The guest keeps
commands until its cgame runs (they used to be counted as seen while it loaded). The guest drops repeated
frames of the same level time (the hold sends several), so its snapshots only move forward.

Joining mid-level is the M1 path (a whole frame); verified after a map change and after a load.

## Saves (step 9)

- Only the host saves (M1: the replica refuses). The `.sav` stays plain single player: player 2 is not
  in it and saved pointers to it are cleared (M0). Verified: a borg2 save made with a guest connected
  loads in a build without co-op, with no warnings and no player 2.
- Player 2's health, armor, weapon and ammo carry over a map change in the `coop_p2save` cvar (M0,
  written by `G_ChangeMap`, read at the next level start). Kept: it is how the game carries player 1 too
  (the playersave cvars), and it needs no file between the two levels.
- New: every save also keeps them in `<save>.coop` next to the `.sav` (the proposal's side file). The
  game puts them in `coop_p2saved` in `WriteLevel`; the engine writes the file after the `.sav` (and
  removes an old one when player 2 has nothing to keep); a load reads it into `coop_p2save` before the
  level starts. So a load, including the death respawn's level-entry autosave, brings player 2 back as
  it was when saved, not fresh. At a level-entry autosave player 2 hasn't spawned yet, so the file holds
  the carry waiting for its spawn. Single player never reads `.coop` files.

## Lag compensation for player 2's shots (step 9a)
The guest sees the host's world about a round trip plus 50 ms late (it draws between the last two host
frames it has). Without help, player 2 aimed at an NPC where the guest's screen showed it and the host
traced the shot against where the NPC was by then. `coop/coop_lagcomp.cpp` follows the idea of cMod's
ping compensation by Noah Metzger (Chomenor): keep the recent past, move the targets back for the shot's
trace only, put them back.
- History: at the end of each host frame (`COOP_EndFrame`), while a guest is connected, the position,
  angles and box of every linked solid client (player 1, NPCs) and every moving brush model, for the last
  32 frames (1.6 s).
- What the guest saw: each guest command carries `viewTime` (protocol 6), the host level time of the frame
  the shot was aimed in. The guest's cgame notes it when its prediction fires (`COOP_PredictShot`: the
  last drawn frame, whose entity positions `CG_Trace` uses), and the bridge sends it with the command;
  a shot the cgame predicts after its command went belongs to the newest drawn frame, which is what is
  sent then.
- The shot: around `FireWeapon` (`COOP_ShotStart` / `COOP_ShotEnd`), for player 2 only and for the
  weapons whose shots are traces (phaser, compression rifle, I-MOD, proton gun, the tetrion's primary,
  the dreadnought's primary, the stasis alt), `gi.trace` is swapped for one that moves the recorded
  entities to their interpolated place at `viewTime`, traces and restores them. At most `coop_lagcomp_max`
  (400 ms) back. Player 2 itself isn't moved; projectiles aren't shifted (they fly in the host's present).
  Player 1's shots never go through it.
- Cvars (host): `coop_lagcomp` (1), `coop_lagcomp_max` (400), `coop_lagcomp_debug` (prints each shifted
  trace with the result without shifting).

Check: `coop_simlatency 100` on both copies (100 ms each way, `sys_coop_net.c`), the guest holds the
rifle's fire at an NPC that `coop_p2_target npc 40 1000` sways 40 units across player 2's view (a test
command: the entity is moved each frame, made unhurt and kept from thinking). 23 shots: the guest's
predicted trace hit 4 times; the host's compensated trace agreed on 23 of 23; without compensation it
agreed on 14 of 23 (it hit 5 times, mostly at other moments). The positions the two traced against agreed
within a unit.

## The guest's own shots at once (step 9b)
`coop/coop_predict.cpp`, after cMod's weapon prediction by Noah Metzger (Chomenor): the client draws its
own shot's effects and leaves out the server's.
- Player 2's movement events on the guest come from one place each: the ones its moves make (footsteps,
  jumps, falls, water, weapon change and fire, out of ammo) from the guest's prediction, everything else
  from the host. Before, a shot's fire event ran up to three times on the guest: when predicted, when the
  host's player state brought it, and from the host's copy of it on player 2's entity.
- For the compression rifle and the I-MOD (both modes), `CG_FireWeapon` on the guest traces the shot
  itself (`CG_Trace`, as the host does from player 2's eye) and draws the beam and the impact (a hit
  flash on a client, the wall impact otherwise) at once. The host's temp entities of a shot fired while
  player 2 fires carry player 2's number (`entityState_t.otherEntityNum2`, unused before), and the guest
  leaves out those of the predicted weapon for a second.
- Already at once without this: the muzzle flash and fire sound of every weapon, and the phaser's and
  arc welder's continuous beams. Not predicted (they show a round trip later): the tetrion's spread
  (random on the host), the stasis beam, and every projectile (scavenger, stasis, grenade launcher,
  tetrion alt, quantum burst, arc welder alt).
- Cvars (guest): `coop_predictshots` (1), `coop_predict_debug`.

Check: in-engine screenshots every 70 ms over a 100 ms link: the first shot's impact shows in the frame
the guest fired; with `coop_predictshots 0` the beam shows about 280 ms later. No second beam or impact
from the host.

## Internet (step 10)
- The Co-op… sheet's Host group has "Open the port on my router": it asks the router to forward UDP
  29760 to this Mac (NAT-PMP / PCP / UPnP through mDNSResponder, `DNSServiceNATPortMappingCreate`, as the
  dedicated server window does) and shows the public address, or why not (unsupported, turned off,
  double NAT, a private outer address, no answer in 15 s) with the manual alternative. The mapping stays
  while the app runs once Host is pressed; Cancel or Connect removes it; quitting removes it.
- Copy Invite copies `stvef://coop/<address>:29760`: the public address when the port is open, else this
  Mac's network address. The launcher's link handler (co-op builds) opens the Co-op… sheet with the
  address filled in and checked; in a game it says to quit and open it again. The app's URL handler
  registration is unchanged.
- Test hooks: `VOYAGERSP_TEST_COOP_INTERNET=1` (tick the box, log the answer and the invite, snapshot,
  Cancel), `VOYAGERSP_TEST_NAT=<address|fail>` (a pretend answer), `VOYAGERSP_TEST_LINK=stvef://coop/...`.
  One real request to this Mac's router: it answered with a public address for UDP 29760; the mapping was
  removed at Cancel.

### Bandwidth
Measured with `coop_netstats 1` (host to guest, steady state; player 1 turning, player 2 walking), and
`coop_netstats 2`, which names the fields that change most. Most of the traffic was fields only the host
uses (command times, the last command, timers, the bone and muzzle points the host's renderer works out)
and entities far from player 2. Now:
- Masked from entity images: player 1's command and timing fields, the clients' render-info points and
  frame numbers (the guest keeps those it computed itself), event times, absolute boxes, the NPCs' last
  command and think times.
- Send rates: an entity more than 1024 units from player 2's eye and outside its PVS is sent every 10th
  frame (the guest keeps the last image meanwhile); within reach, in a cutscene, or player 2 itself,
  every frame.

| map | before | after |
|---|---|---|
| borg2 | 23.8 KB/s | 4.3 KB/s |
| borg1 | 15.3 KB/s | 3.9 KB/s (4.5 with the briefing screen) |
| voy1 | 12.5 KB/s | 3.0 KB/s |

About 1 KB/s of that is packet headers (20 packets a second).

## Also fixed
- "Bad animFileIndex" on the guest: the view weapon looked up `cgs.clientinfo[clientNum]`, which holds
  player 1 only (`MAX_CLIENTS` 1); on the guest the view is player 2's, so it read past the array. Player 2
  now uses its own client info, as `CG_ResetPlayerEntity` already did for clients past the array.
- Set-based mover sounds (the sound sets load since the single-player fix): borg1's lift played
  `bigelevatorstart` and `bigelevatorstop` on both copies (`s_show 1`); the guest's handles come from
  its own sets (M1's `RBMS` stage).

## Testing (two copies on one Mac)
Scratch homes, `+set ui_initialsetup 1 +set com_introplayed 1`, a test bundle with co-op, stdin FIFOs for
console input, `screencapture -l` of the two windows. Two tips: the windows must not cover each other
completely (a fully covered window stops presenting and its game stalls), so the two copies get different
shapes; and a `map` typed while the startup logo plays drops to the menu when the logo ends (single-player
behaviour), so wait for it.
- borg1 New Game with the guest connected first: held 1.4 s, then the opening camera, crawl and its
  colours, the same shots on both; skip on the host (Use Item) ends the scene on both; player 2 next to
  player 1 afterwards.
- voy1 New Game: the opening movie (st_01) started on the guest while the host's played and stopped with
  it; the next one (st_02) played on both from the start.
- borg1 to borg2 with `use t96` (the level's target_level_change, through `G_ChangeMap`): the guest
  followed, player 2 kept health 70; `save` wrote the `.coop`; damage, then `load`: player 2 back at 70;
  the guest disconnected and rejoined mid-level with the same view.
- Regression harness vs `coop-m2-base` (86d67cd): ALL IDENTICAL with co-op off and with co-op on and
  `sp_coop 0`.

Not tested: two Macs; a movie the host skips (the guest follows the host's resume, the same path as a
movie that ends); a guest that never finishes loading (the 30 s limit).

## Combat with two players, downed players (2026-11-11)
Enemies (`efgame/src/coop/coop_ai.cpp`):
- Level scripts name the player as an NPC's enemy by the name only player 1 has. In co-op such a script
  gets the player who hurt that NPC last (within 5 s), else the one it already fights, else the nearer
  one. This is what made Borg drones that player 2 shot turn on player 1 (their pain script sets the
  enemy to the player).
- An NPC fighting one player turns on the other when that one is in sight and either the current one
  isn't or the other is clearly nearer (75% of the distance); checked twice a second, held 3 s after a
  switch. Shot by the other player: turns on the shooter with the game's skill-based odds.
- Test commands: `coop_p2_npcs [radius]` lists NPCs and their enemies, `coop_p2_npc spawn <type>` (the
  game's `npc` command), `coop_p2_aim <n>`, `coop_p2_use [frames]`, `coop_p2_damage <n> [fall|pit] [p1]`.

Downed players (`efgame/src/coop/coop_down.cpp`, `coop_hud.cpp`):
- A player whose health runs out goes down: lies on the floor (frozen, health 0, not a target, not
  solid), and the partner revives them by holding Use within 96 units for `coop_revivetime` seconds
  (default 3); back up with 25% health. Bleed-out `coop_bleedout` (default 30 s; paused during cameras).
- Game over for both: both down, a bleed-out, or player 1 dying for real (scripted mission failure,
  nobody up to revive): player 2 dies with player 1, so the guest gets the same mission status screen;
  either player's Attack reloads.
- Deaths a body can't stay at (trigger_hurt, lava, slime, drowning, crushed, telefrag, falling, no floor
  below): the downed player is put next to the partner (game over when there is no spot).
- Player 2 goes down only while a guest steers it; the local puppet keeps its old death and 4 s respawn,
  and revives player 1 by itself when next to them. A guest leaving while player 2 is down: the old
  death and respawn.
- On screen: the game's own centre print isn't drawn in single player, so the host sends `coopmsg`
  lines that the cgame draws (downed: "Downed: waiting for <name>" and the seconds left; partner:
  "Hold Use to revive <name>", a progress bar while holding).
- Saving is refused while someone is down (`GameAllowedToSaveHere`; a checkpoint autosave waits): a save
  holds player 1 only, and one with player 1 at 0 health would load as a death. A level change revives
  anyone down first.

Tested on one Mac (two copies, the forge3 save): the reaver fight with both players (enemies take
player 2), a spawned reaver switching from player 1 to player 2 when player 2 is next to it, a borg2
drone ignoring the players turning on player 2 after player 2 shot it; down and revive both ways, both
down and bleed-out (mission status on both, the guest's Attack reloads), trigger_hurt death next to the
partner, puppet revive, save refused while down. Regression harness vs `coop-m2-base`: ALL IDENTICAL
with co-op off and with co-op on and `sp_coop 0`.

## Revives and the level's barriers (2026-11-11)
- A revive needs a clear line between the two bodies (at the middle of the body, or 20 units higher,
  so a low console between them doesn't matter): walls, closed doors, forcefields and other movers and
  player clips stop it; people don't. Before, any partner within 96 units revived, and on voy2 player 1
  revived player 2 through the forcefield that seals the burning section, so player 2 got up on the far
  side.
- A player the level itself downs (no enemy as the attacker: trigger_hurt, a mover or forcefield, a
  scripted explosion such as voy2's behind that forcefield, the world) is put next to the partner, as
  for pits, so a body is never left where the story has just closed the way. A player an enemy downed
  stays where they fell. The floor check ignores people and a box starting a little inside a raised
  floor plate.
- Not handled: a player an enemy downs in an area that a barrier closes afterwards can't be reached
  and bleeds out (game over, the host reloads).
- Test: `coop_p2_damage <n> npc` deals the damage as the first enemy NPC in the level (spawn one with
  `coop_p2_npc spawn <type>`); without it the damage comes from the world.
- Testing two copies next to a third that already hosts (it holds UDP 29760):
  `VOYAGERSP_TEST_COOP_PORT=<n>` for the test host, `coop_connect 127.0.0.1:<n>` on the guest.
- The guest's window closing on its own was Cmd+W: SDL's Window menu has Close (Cmd+W), which asks to
  close the only window, and SDL then quits ("Client Shutdown (Closed window)"). The two
  `MAX_PACKET_USERCMDS` lines after it are the disconnect packets carrying the commands single player
  never sends; harmless.
- Two copies on one data folder (a host and its guest on one Mac): the PID file now belongs to the copy
  that wrote it. A copy that finds another running copy's PID leaves the file alone, and a copy only
  removes the file when it still holds its own PID. Before, the second copy overwrote it and the first to
  quit removed the other's, which could make a later start ask about an abnormal exit. A copy that
  crashes alone is still caught.
- Playing in the background: `coop_backgroundPlay` (archived, default 1; the Co-op sheet's Host side
  has "Keep playing in the background", on by default, passed as `+set coop_backgroundPlay 0|1`).
  While this copy hosts (single player, `sp_coop 1`) or is a guest, the unfocused/minimized frame caps
  (`com_maxfpsUnfocused`, `com_maxfpsMinimized`) are skipped and the process holds a latency-critical
  NSProcessInfo activity (no App Nap or timer coalescing when its window is covered). Keys and the
  mouse still go to the front window only. Measured with `com_maxfpsUnfocused 5` on both unfocused
  copies: 20 host frames/s with it on, 10 with it off. Single player has no pause of its own on focus
  loss. No effect without co-op (Holomatch, `sp_coop 0`, EF_COOP off).
