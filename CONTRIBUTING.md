# Contributing to EliteForce-MacOS

Thanks for helping. This is a small, non-commercial fan project. Bug reports from real
playthroughs are the most useful contribution right now.

## Reporting issues

Open a GitHub issue and include:

- Mac model (for example M1 Pro) and macOS version.
- Where your game data comes from (GOG, Steam, CD + patch 1.2, ...) and which `pak*.pk3`
  files are in your `BaseEF`.
- App version (release tag or git commit).
- Steps to reproduce: the map (`/mapname` in the console), what you did, and what happened.
  Attach a save if the problem depends on one.
- The console log (see "Reporting issues" in the [README](README.md)).

Please **do not attach or link game data** (`.pk3`, `.bik`, retail executables). Those are
copyrighted and can't be shared here.

## Pull requests

- Keep changes focused. Separate commits for separate fixes, with a message explaining *why*.
- Don't break the Android build. Shared engine and game code (`EFAndroid-SP/app/jni/efcode`,
  `EFAndroid-SP/app/jni/sp`, `efgame/`) is compiled for Android too. Guard platform-specific
  code with `#ifdef __APPLE__` / `#ifndef __ANDROID__`, as the existing macOS changes do.
  Fixes that apply to every platform are welcome upstream at
  [VoyagerSP-Android](https://github.com/imjustadudegamer/VoyagerSP-Android) too.
- Build both CMake projects (see the README) and run the game before submitting.
- **Licensing:** contributions to the engine (`EFAndroid-SP/`) are accepted under the
  GPL-2.0-or-later. Changes to `efgame/` fall under Raven's Limited Use Software License
  (non-commercial, free distribution only). By submitting a PR you agree your change can be
  distributed under the license of the files you touch.
- No game assets, and no Star Trek / Paramount logos or artwork, in the repository.
