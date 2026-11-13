Elite Force for Linux and Steam Deck @VERSION@ - Marino's Build (EliteForce-MacOS-Marino)
Star Trek: Voyager - Elite Force single player and Holomatch for x86_64 Linux, as a single AppImage.
https://github.com/marinohardin/EliteForce-MacOS-Marino

The Linux version is EXPERIMENTAL: it is new and much less tested than the Mac version.
Please report problems on the GitHub page above.

You need:
- Linux x86_64 (glibc 2.31 or newer, e.g. Ubuntu 20.04+, Fedora, Arch) or a Steam Deck (SteamOS).
- A GPU and driver with Vulkan 1.1 (Mesa RADV/ANV, or the NVIDIA proprietary driver).
- Your own copy of the game (GOG, Steam or CD + patch). No game data is included.

Run:
1. chmod +x EliteForce-Marino-@VERSION@-x86_64.AppImage   (or tick "Allow executing as program")
2. Double-click it, or run it from a terminal. The launcher opens.
   If your system lacks FUSE 2:  APPIMAGE_EXTRACT_AND_RUN=1 ./EliteForce-Marino-@VERSION@-x86_64.AppImage
3. Under Game Files, click Choose... and pick your Elite Force folder (the one containing BaseEF),
   the BaseEF folder itself, or GOG's offline installer (setup_...exe). The game files are copied
   into ~/.local/share/eliteforce-marino/baseEF/ (about 600 MB); tick "Use BaseEF files in place
   rather than copy" to read them where they are instead. A GOG install under Heroic, Lutris or
   another Wine prefix is usually found by itself. Add Game Files... adds more .pk3 files, such as
   the Expansion Pack's or extra maps.
4. Pick Single Player or Holomatch and click Play.

LAN parties: everyone picks Holomatch. One person hosts (Host a Server in the launcher,
or Servers > Create Server in the game); the others find it under Servers > Local.

Co-op is not in the Linux version (Mac only for now).

Without the launcher (Steam, scripts), the AppImage takes these options:
     --sp            single player
     --holomatch     Holomatch (multiplayer)
     --server        dedicated server for the internet (run it in a terminal)
     --server-lan    dedicated server for the LAN
     --uri stvef://connect/host:port    join a server
     --launcher      the launcher (also when it is set to start the last mode by itself)
   Any other arguments go straight to the game, e.g. +set r_mode 6.
Game files can also be copied by hand into ~/.local/share/eliteforce-marino/baseEF/
(or $XDG_DATA_HOME/eliteforce-marino/baseEF), or named with  --sp +set fs_basepath /path/to/folder
(the folder that contains BaseEF). If your file system is case-sensitive and a file is not found,
use lowercase file names.

Steam Deck:
1. Switch to Desktop Mode and download the AppImage (e.g. into ~/Applications).
2. In Steam: Games > Add a Non-Steam Game to My Library > Browse, set the filter to All Files, pick
   the AppImage and add it. Open its Properties and set Launch Options to  --sp  (or --holomatch;
   add the game twice if you want both).
3. Copy your game files into ~/.local/share/eliteforce-marino/baseEF/ (in Dolphin, Ctrl+H shows
   hidden folders).
4. Controller: pick the "Gamepad with Mouse Trackpad" template (Controller Settings > Browse Configs >
   Templates). Menus work with the D-pad / A / B, and the trackpad moves a pointer.
5. Go back to Gaming Mode and start it from your Library. On the first run it goes fullscreen at the
   Deck's resolution and applies gamepad controls: left stick moves, right stick looks, triggers fire,
   A jump, B crouch, X use, Y use held item, bumpers change weapon, Start opens the menu. They are
   ordinary key bindings: change them under Controls, or edit baseEF/deck.cfg in the folder above and
   delete ~/.local/share/eliteforce-marino/.deck-applied-sp (or -hm) to apply the file again.

Menu entry and stvef:// links: AppImageLauncher or Gear Lever can integrate the AppImage (menu entry with
Single Player / Holomatch actions, and stvef:// links).

cMod's game modules by Noah Metzger (Chomenor) are included for Holomatch, with his permission.

Licenses are in usr/share/licenses/eliteforce-marino inside the AppImage (--appimage-extract unpacks
it). The single-player game code is Raven Software's, under its Limited Use Software License (free,
non-commercial, for owners of the retail game only). Star Trek and related marks are trademarks of
Paramount; this is an unofficial fan project, not affiliated with or endorsed by Paramount, Raven
Software or Activision.
