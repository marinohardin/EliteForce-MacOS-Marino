Elite Force for Windows @VERSION@ - Marino's Build (EliteForce-MacOS-Marino)
Star Trek: Voyager - Elite Force single player and Holomatch for 64-bit Windows.
https://github.com/marinohardin/EliteForce-MacOS-Marino

The Windows version is EXPERIMENTAL: it is new and much less tested than the Mac version.
Please report problems on the GitHub page above.

You need:
- Windows 10 or 11, 64-bit.
- A GPU and driver with Vulkan 1.1 (any recent NVIDIA, AMD or Intel driver).
- Your own copy of the game (GOG, Steam or CD + patch). No game data is included.

Install:
1. Installer (-setup.exe): run it. It installs for your user only (no administrator rights) and
   adds Start menu entries and the stvef:// link handler for Holomatch server links.
   Zip: unpack it anywhere you can write to (not Program Files) and run EliteForce.exe.
2. Windows SmartScreen: the program is not code-signed, so Windows may say "Windows protected
   your PC". Click "More info", then "Run anyway".
3. The launcher opens. Under Game Files, click Choose... and pick your Elite Force folder (the
   one containing BaseEF), the BaseEF folder itself, or GOG's offline installer (setup_...exe).
   The game files are copied into %APPDATA%\EliteForce-Marino\baseEF (about 600 MB), so the
   original folder or CD is no longer needed; tick "Use BaseEF files in place rather than copy"
   to read them where they are instead. An installed GOG or CD copy is usually found by itself.
   Add Game Files... adds more .pk3 files, such as the Expansion Pack's or extra maps.
4. Pick Single Player or Holomatch and click Play.

LAN parties: everyone picks Holomatch. One person hosts (Host a Server in the launcher,
or Servers > Create Server in the game); the others find it under Servers > Local.
Allow access when Windows Defender Firewall asks.

Co-op (experimental): play the single-player campaign with a friend. Click Co-op... on the
Single Player card. One PC hosts (Host, then New Game or Load as usual); the friend types the
host's address, clicks Check, then Connect, and plays player 2. Both need the same version of
Elite Force (Marino's Build) and the game's own pak0.pk3-pak2.pk3; a Mac or Linux player of the
same version should be able to join too. Co-op uses UDP port 29760: allow access when Windows
Defender Firewall asks on the host. For a friend on the internet, tick "Open the port on my
router" and send them the Copy Invite link. Co-op hasn't been tested on Windows yet.

Without the launcher (shortcuts, scripts):
  Single player:    EliteForce.exe +set com_holomatch 0
  Holomatch:        EliteForce.exe +set com_holomatch 1
  Dedicated server: EliteForce.exe +set com_holomatch 1 +set dedicated 2     (1 = LAN only)
  Join a server:    EliteForce.exe --uri stvef://connect/host:port
Settings, saves and the copied game files are in %APPDATA%\EliteForce-Marino
(paste that into the File Explorer address bar).

The game window's close button asks before quitting.

Holomatch includes cMod's game modules by Noah Metzger (Chomenor), with his kind permission:
a widescreen HUD, lag compensation on servers you host, and many fixes. Thank you, Noah.

Licenses are in the licenses folder next to EliteForce.exe. The single-player game code is
Raven Software's, under its Limited Use Software License (free, non-commercial, for owners of
the retail game only). Star Trek and related marks are trademarks of Paramount; this is an
unofficial fan project, not affiliated with or endorsed by Paramount, Raven Software or
Activision.
