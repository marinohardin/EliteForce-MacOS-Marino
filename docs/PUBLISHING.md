# Publishing checklist (maintainer notes)

Internal notes for turning this fork into its own public repository and shipping the first
`.app` release. Not user-facing.

## 0. Name

The repository is **`EliteForce-MacOS-Marino`** (first `VoyagerSP-MacOS`, then `EliteForce-MacOS` once Holomatch joined
single player); the app is **Elite Force.app**. The old name still appears on purpose as the
legacy data folder name (`MACOS_LEGACY_HOMEPATH_NAME`) and in historical paths in
`docs/macos-port-notes.md`. The data folder keeps its name, `Application Support/EliteForce-MacOS`.

Avoid "Star Trek" in the repo name, app name and icon (Paramount trademark). "Elite Force" and
"Voyager" as descriptive references are the norm for fan ports (VoyagerNX, VoyagerSP-Android).

**GitHub description:** "Native macOS (Apple Silicon) port of Star Trek: Voyager – Elite Force
single-player and Holomatch multiplayer. Vulkan via MoltenVK, Retina support. Requires the retail game data."

**Topics:** `elite-force`, `star-trek-voyager`, `idtech3`, `ioquake3`, `quake3`, `macos`,
`apple-silicon`, `arm64`, `vulkan`, `moltenvk`, `source-port`, `game-port`.

## 1. History and upstream relationship

Options:

| Option | Pros | Cons |
|---|---|---|
| **A. New repo, full history kept, `upstream` remote** | Authorship and attribution live in the history. Easy to merge upstream fixes. Standalone repo (own issues, searchable, not labelled "fork"). | Repo carries Android history (size). |
| B. GitHub "Fork" of VoyagerSP-Android | One-click, visible "forked from" link | Forks are de-prioritized in GitHub search, can't easily be detached later, and a different account can only have one fork. PR base defaults to upstream. |
| C. Fresh repo, squashed history plus attribution | Small and clean | Loses per-commit authorship of upstream work. Harder to merge upstream. Weaker GPL provenance. |

**Recommendation: A.** Push the full history (including upstream commits up to `0d8942e` and the
`macos-port` work) to a new repo. Keep `upstream` as a remote:

```sh
git remote rename origin upstream          # upstream = imjustadudegamer/VoyagerSP-Android
git remote add origin git@github.com:<owner>/<name>.git
git push -u origin macos-port:main         # the merged macOS branch becomes main
git push origin --tags                     # only if the upstream tags should be carried over
```

Credit upstream visibly: the README "Origins" section and the top of CREDITS.md already do this.
Consider also opening an issue or discussion on VoyagerSP-Android to say the port exists, and offer
the platform-neutral fixes (the items.dat hole fix and the `vmMain` fixed-args call) back upstream.

Default branch: upstream uses `main`, and the macOS work lives on `macos-port` (plus the feature
branches merged into it). Publish it as `main` in the new repo so the default branch shows the
macOS README. The local `main` tracks upstream and can be renamed `upstream-main`.

## 2. License compliance before shipping binaries

- [ ] **GPL source for the engine.** The release must come with, or point to, the exact
      corresponding source. Simplest: tag the commit the `.app` was built from, and say in the
      release notes and in an About/README file inside the zip that "source is at
      <repo>/tree/<tag>". Keep the tag forever. Include `LICENSE` (GPL text) in the zip.
- [x] **FFmpeg is LGPL.** `macos/build-ffmpeg.sh` builds a minimal LGPL-2.1+ FFmpeg (Bink only,
      no external libraries) and `make-app.sh` bundles it with its license text and source note in
      `Contents/Resources/licenses`. When bumping `FFMPEG_VERSION`, update the SHA-256, the version
      in THIRD_PARTY_LICENSES.md, and check that cinematics still play.
- [ ] **The `efgame/` (Raven) license terms.** Free of charge, non-commercial, electronic
      distribution only, include the license document with each copy, for use with the full
      retail game. So: no donations or Patreon gating for downloads, no ads, no app-store
      listing. Put `STEF Game Source License.doc` (or a faithful .txt/.pdf conversion plus the
      original) into the release zip next to the app.
- [ ] **GPL plus non-GPL module.** The engine (GPL) dlopens Raven's non-GPL game modules. This is
      the same arrangement as every Elite Force/Q3 source port and the Android upstream, and is
      widely tolerated. Say plainly in the README that the two parts are under different licenses
      (done). Don't relicense `efgame/`.
- [ ] **Apache-2.0 (MoltenVK, Vulkan-Loader):** include the license text and any NOTICE files.
- [ ] **zlib (SDL3, sdl2-compat):** include the notices (recommended, not strictly required for
      binaries).
- [ ] **libmad (GPL) and IJG libjpeg:** compiled in. Covered by the GPL source offer plus the IJG
      acknowledgement in THIRD_PARTY_LICENSES.md.
- [ ] Put a `licenses/` folder or `THIRD_PARTY_LICENSES.md` plus license texts inside the zip
      (or in `Contents/Resources/` of the app).
- [ ] **No game assets** in the repo, the app or the release. Check with
      `git ls-files | grep -iE '\.(pk3|bik|roq|dll|exe)$'` (expected: none) and inspect the
      bundle.
- [ ] **Trademark:** no Star Trek or Paramount logos, Elite Force box art, or LCARS artwork
      lifted from the game in the app icon, screenshots used as marketing, or repo social
      preview. Use an original icon. Keep the "unofficial, non-commercial fan project, not
      affiliated" line in the README and release notes.

## 3. Release steps

1. Everything merged into `main`; smoke-test with retail data: a new game, a PC save, a
   cinematic, windowed / fullscreen / Retina, a Holomatch bot match, Host a Server.
2. Bump `VERSION` in `macos/make-app.sh` (the app's version, e.g. 1.1.0).
3. `macos/make-release.sh` builds the app (`macos/make-app.sh`: self-contained, ad-hoc signed,
   it fails if anything still points outside the bundle) and packages it as
   `dist/EliteForce-MacOS-Marino-<version>-arm64.dmg` (app, Applications shortcut, Read Me) and
   `.zip` (app, Read Me), both made with `ditto` / `hdiutil` so signatures survive. The Read Me
   template is `macos/release/ReadMe.txt`; the license texts ship inside the app.
4. Signing: ad-hoc for now (users right-click → Open the first time). With a Developer ID:
   sign with the hardened runtime, `xcrun notarytool submit … --wait`, `xcrun stapler staple`
   (the app, then the DMG), and Gatekeeper opens it without warnings.
5. Draft the release on the tag `macos-v<version>` (not `v<version>`: the Android project's
   `v1.x` tags are in this repo's history) targeting `main`, as a pre-release while in beta:
   `gh release create macos-v<version> dist/EliteForce-MacOS-Marino-<version>-arm64.{dmg,zip}
   --draft --prerelease --target main --title "Elite Force for Mac <version>" --notes-file notes.md`.
   Notes: what's new, install (DMG drag, Gatekeeper), requirements (Apple Silicon, macOS, retail
   data), known issues, licenses and credits.
6. Review the draft on GitHub and publish. Publishing creates the tag.

## 4. Open doc TODO markers

`git grep -n 'TODO('` lists any; there are none at the moment. Parked ideas live in
`docs/TODO.md`.
