Elite Force for Windows - Marino's Build (EliteForce-MacOS-Marino)
Single player and Holomatch for Raven Software's Elite Force.
https://github.com/marinohardin/EliteForce-MacOS-Marino

You need:
- Windows 10 or 11, 64-bit.
- A GPU and driver with Vulkan 1.1 (any recent NVIDIA, AMD or Intel driver).
- Your own copy of the game (GOG, Steam or CD + patch). No game data is included.

Install:
- Installer (-setup.exe): run it. It installs for your user only (no administrator rights) and adds
  Start menu entries for Single Player, Holomatch and a Dedicated Server, and the stvef:// link handler.
- Zip: unpack it anywhere and run EliteForce.exe (Single Player is the default).
  Holomatch:        EliteForce.exe +set com_holomatch 1
  Dedicated server: EliteForce.exe +set com_holomatch 1 +set dedicated 2     (1 = LAN only)
  Join a server:    EliteForce.exe --uri stvef://connect/host:port

Windows SmartScreen: the program is not code-signed yet, so Windows may say "Windows protected your PC".
Click "More info", then "Run anyway".

Game files (pak0.pk3 ... pak3.pk3 and the other .pk3 files, from the game's BaseEF folder):
Put them in  %APPDATA%\EliteForce-Marino\baseEF   (paste that into the Explorer address bar)
or in a BaseEF folder next to EliteForce.exe. A GOG or Steam install of the game is found as well;
otherwise point the game at it with
    EliteForce.exe +set fs_basepath "D:\Games\Elite Force"
(the folder that contains BaseEF).

cMod's game modules by Noah Metzger (Chomenor) are included in the cmod folder for Holomatch, with his
permission.

Licenses are in the licenses folder. The single-player game code is Raven Software's, under its Limited
Use Software License (free, non-commercial, for owners of the retail game only). Star Trek and related
marks are trademarks of Paramount; this is an unofficial fan project, not affiliated with or endorsed by
Paramount, Raven Software or Activision.
