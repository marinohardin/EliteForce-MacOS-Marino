cMod game modules
=================

This folder of the app (Contents/Resources/cmod) holds cMod's game modules
by Noah Metzger (Chomenor): pakcmod-release-2023-09-18.pk3, his released
cgame, ui and qagame for Star Trek Voyager: Elite Force Holomatch. They
are included unchanged, with his kind permission. Huge thanks, Noah.

  cMod:        https://stvef.org/cmod/
  Engine:      https://github.com/Chomenor/ioef-cmod
  Game code:   https://github.com/Chomenor/cmod-game-code

What they bring to Holomatch in Elite Force for Mac:
  - a widescreen HUD and menus (cg_aspectCorrect, cg_aspectCorrectCenterHud);
  - lag compensation and smoothing on servers you host (g_unlagged,
    g_smoothClients), for every player whatever game they run;
  - weapon fire prediction on servers that offer it (cg_predictWeapons,
    g_unlaggedPredict);
  - many bug fixes in the game code.

The app recognizes the pk3 by its checksum (cMod's own list of releases),
uses its modules in place of the stock ones (patch 1.2's pak2.pk3), and
never in place of a mod's own modules or on a pure server. A server never
lists this pk3 or sends it to players. Turn it off in the launcher
(Holomatch: "Use cMod's game modules"; Host a Server: Config..., cMod tab).

The game modules are built from Raven Software's Star Trek Voyager: Elite
Force game source, under Raven's Limited Use Software License Agreement:
free, non-commercial distribution only, for use with the retail game. Its
text is "STEF Game Source License.doc" in this folder (and in
../licenses). They are not covered by the engine's GPL.

For building: the cMod pk3 is kept in git here (macos/cmod/), unlike other
pk3 files. macos/make-app.sh, windows/make-release.sh and
linux/make-appimage.sh copy the newest macos/cmod/pakcmod-release-*.pk3
(or the file named by CMOD_PAK=<path>) into the package, with this README
and the license. For a new cMod release, add its pk3 here and remove the
old one.
