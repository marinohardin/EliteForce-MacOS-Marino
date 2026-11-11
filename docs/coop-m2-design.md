# Co-op M2: cutscenes, level changes, saves (as built)

Status: steps 7, 8 and 9 of the plan are in (2026-11-10, branch `1_4_0/coop-m1`). Steps 9a (lag
compensation for player 2's shots), 9b (the guest's own fire effects at once) and 10 (internet: router
port, invite, bandwidth trimming) are next. Background: `docs/coop-proposal.md` (the host runs the game,
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
