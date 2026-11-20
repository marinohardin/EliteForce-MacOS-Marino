**AwayTeamEF @VERSION@**: *Star Trek: Voyager – Elite Force* for **Mac, Windows and Linux** (Steam Deck too), with **co-op for the single-player campaign**, Holomatch multiplayer and the full single-player game. Formerly "Elite Force (Marino's Build)".

> **No game data is included.** You need your own copy of the game (GOG, Steam, or CD + 1.2 patch).

### Downloads
| Platform | File | How to start |
|---|---|---|
| **Mac** (Apple Silicon) | **EliteForce-MacOS-Marino-@VERSION@-arm64.dmg** (or the .zip) | Drag the app to Applications and open it. Signed and notarized by Apple. |
| **Windows** (64-bit) | **EliteForce-Marino-@VERSION@-setup.exe** (installer) or **EliteForce-Marino-@VERSION@-windows-x64.zip** | Run the installer, or unzip anywhere and run EliteForce.exe. **Not code-signed yet:** if SmartScreen warns, choose "More info", then "Run anyway". |
| **Linux** (x86_64, Steam Deck too) | **EliteForce-Marino-@VERSION@-x86_64.AppImage** | `chmod +x` it and run it. |

> **Windows:** if Windows reports "Bad Image … 0xc0e90002", Smart App Control is blocking the unsigned build. Code signing will be in a future build. You may have to search for how to work around this.

The launcher on each platform helps you point the game at your data. **Every platform plays co-op with every other**: a Mac can host a Windows PC or a Steam Deck, and the other way round. Both players need @VERSION@.

## What's new in @VERSION@

### Highlights
- **Cross-platform co-op for the single-player campaign.** It's no longer experimental. A friend joins your campaign as a second Hazard Team member, on your network or over the internet, from a Mac, a Windows PC or a Linux PC / Steam Deck.
- **Co-op on Windows and Linux.** The Co-op sheet is in all three launchers, with the same features.
- **A new name: AwayTeamEF.** The launcher header has Star Trek lettering.
- **Smoother player 2.** The guest moves on their own clock, so walking stays smooth even on Wi-Fi.
- **Big game folders work.** A BaseEF with thousands of .pk3 files loads.

### Co-op
- **Auto-detects games on your network.** Click one to check it and join.
- **Copy Invite** makes a link that opens the Co-op sheet on your friend's computer, on any platform.
- **Player 2 can choose from a large number of character models**, including experimental ones from Holomatch. The voice stays the Munro you chose.
- **Team attack:** the host chooses whether players can hurt each other: **Off** (the default), **Half** or **On**.
- **Hold Use to bring player 2 to you**, and **double-tap Use to ping** a spot for your partner.

### Co-op fixes from Experimental
- **Player 2 feels like single player:**
  - Movement is predicted on the guest's own clock, so the view no longer freezes and jumps when the network delivers frames late.
  - The view bob is smooth.
  - Shots leave from the eye, not the hip.
  - More weapons fire at once on the guest.
  - Crouching holds even when the network is late.
- **Player 2 sees everything:** some effects were not playing on player 2's screen.
- **Reviving a partner while standing on top of them** no longer leaves you stuck inside each other.
- **Saving works while a player is down.**
- **After a death**, the game reloads the save you loaded last.
- **Levels fixed** include the Borg missions, the Forge and the Dreadnought: space walks, gravity zones, crawlspaces, sealed doors and scripted moves.
- **Player 2 follows player 1** through scripted moves and teleports, keeps the health, armor and bolt-ons that scripts give, and rejoins in cover.

### Windows
- **Check finds the host again**, instead of saying "This address can't be used".
- **The Co-op sheet's text** is no longer cut off by the Host button.
- **More stable:**
  - The launchers' background tasks no longer touch closed windows.
  - A name with an accented character (such as "é") no longer crashes the game.
  - Fonts stay right when you move a window to another monitor.
  - A `stvef://` link during a data copy no longer freezes the launcher.
  - Server window Kick picks the right player.
- **The launcher's server summary** shows "Server name" and "Internet games" correctly. The gear menu opens under its button.

### Linux & Steam Deck
- **The Steam Deck touch screen works in the menus.**
- **The Co-op sheet's network list** shows each game's host.
- **The window title** reads correctly.
- **The launcher looks right** in dark desktop themes and at 2× scaling.

### Mac
- **Choosing the game folder:** selecting BaseEF (or the game folder) and clicking Choose accepts it, instead of opening the folder.
- **Text fields:** Cmd+A, Cmd+Z and cut/copy/paste work in the launcher's text fields.
- **Joining:** leaving the host's address field no longer starts a Check, and Connect is the default button once a game is found.

### Single player & everyone
- **Big BaseEF folders:** thousands of .pk3 files load. There's no more file-list, memory or open-file limit, and a clear message appears if the system's open-file limit is reached.
- **The brig cutscene** no longer restarts its opening shot.
- **No crash** when attacking with a melee weapon while the game is slowed down.
- **After a death**, the reload goes back to the save you loaded last.
- **The player model preview** in the menus has the right field of view.

Thanks to **Noah Metzger (Chomenor)** for cMod's game modules, used by Holomatch.
