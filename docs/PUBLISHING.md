# Publishing checklist (maintainer notes)

Internal notes for turning this fork into its own public repository and shipping the first
`.app` release. Not user-facing.

## 0. Pick the name, then replace the placeholder

The docs use **`VoyagerSP-MacOS`** everywhere as a placeholder. Once the name is final:

```sh
git grep -l 'VoyagerSP-MacOS' | xargs sed -i '' 's/VoyagerSP-MacOS/NEWNAME/g'
git grep -n 'OWNER/'          # release link placeholder in README.md
git grep -n 'TODO('           # every open doc TODO (see the list at the end)
```

(`VoyagerSP-macos` in lowercase also shows up in historical paths in `docs/macos-port-notes.md`.
Leave those alone.)

Suggested names:

1. **`VoyagerSP-MacOS`**: mirrors the upstream `VoyagerSP-Android` and makes the lineage obvious.
   (Recommended.)
2. **`EliteForceSP-macOS`**: easier to find by people searching for "Elite Force", and avoids
   leaning on "Voyager". Still descriptive, not a logo or trademark.
3. **`lilium-voyager-sp-mac`**: emphasizes the engine lineage. Less discoverable.

Avoid "Star Trek" in the repo name, app name and icon (Paramount trademark). "Elite Force" and
"Voyager" as descriptive references are the norm for fan ports (VoyagerNX, VoyagerSP-Android).

**GitHub description:** "Native macOS (Apple Silicon) port of Star Trek: Voyager – Elite Force
single-player. Vulkan via MoltenVK, Retina support. Requires the retail game data."

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
- [ ] **FFmpeg is GPL-3.0+ when taken from Homebrew.** Homebrew builds it with `--enable-gpl
      --enable-version3` and links x264, x265, OpenSSL, LAME, SVT-AV1, libvpx, dav1d, Opus and
      xz (see THIRD_PARTY_LICENSES.md). Bundling that means shipping roughly ten extra dylibs,
      GPLv3 obligations, and offering the source of all of them. **Strongly recommended
      instead:** build a minimal FFmpeg for the bundle:
      ```sh
      ./configure --prefix=$PWD/../ffmpeg-min --arch=arm64 --enable-shared --disable-static \
        --disable-everything --disable-programs --disable-doc --disable-network \
        --disable-autodetect --enable-avformat --enable-avcodec --enable-swscale --enable-swresample \
        --enable-demuxer=bink --enable-decoder=bink,binkaudio_rdft,binkaudio_dct \
        --enable-protocol=file
      ```
      This is LGPL-2.1+ with no external dependencies. Add `--enable-demuxer=roq
      --enable-decoder=roq,roq_dpcm` if RoQ is ever routed through FFmpeg. Point the engine CMake
      at it via `PKG_CONFIG_PATH`. LGPL still requires: dynamic linking (or relinkable objects),
      the LGPL text, a link to the exact FFmpeg source/version, and the configure line. Record all
      of this in THIRD_PARTY_LICENSES.md. Verify that cinematics still play.
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

1. Merge the `app-bundle`, `video-settings`, `cleanup` and `repo-docs` branches. Resolve the
   doc TODOs listed below.
2. Clean build both CMake projects (Release). Smoke-test with retail data: new game, load a PC
   save, a cinematic, windowed to fullscreen, Retina.
3. Build the bundle (`macos/make-app.sh`, TODO verify) and check it is self-contained:
   `otool -L` on every Mach-O in the bundle should show no `/opt/homebrew` paths. Test on a
   second Mac or user account without Homebrew if possible.
4. Sign:
   - **Ad-hoc** (free): `codesign --force --deep --sign - VoyagerSP-MacOS.app`. Users must
     right-click → Open, or run `xattr -dr com.apple.quarantine VoyagerSP-MacOS.app`.
   - **Developer ID + notarization** (paid Apple Developer account): sign with hardened runtime,
     then `xcrun notarytool submit ... --wait` and `xcrun stapler staple`. Gatekeeper then opens
     it without warnings. Consider this before a wide announcement.
5. Zip with `ditto -c -k --keepParent VoyagerSP-MacOS.app VoyagerSP-MacOS-vX.Y.Z-arm64.zip`
   (`ditto` keeps symlinks and signatures; plain `zip` can break them). Add `LICENSE`,
   `THIRD_PARTY_LICENSES.md`, the Raven license document and a short `README.txt` (where to put
   `BaseEF`, Gatekeeper note, source link).
6. Tag: `git tag -a vX.Y.Z -m "VoyagerSP-MacOS vX.Y.Z"` and `git push origin vX.Y.Z`.
7. Create the GitHub release from the tag. Attach the zip and its SHA-256
   (`shasum -a 256 *.zip`). In the notes include: requirements (Apple Silicon, retail data,
   `pak0.pk3` from the CD for minimum installs), the Gatekeeper workaround, known issues, a
   source link (the tag), license summary and credits to upstream.
8. Update the README release link (`TODO(release-link)`) and add screenshots.

## 4. Open doc TODO markers

Run `git grep -n 'TODO('` for the current list. At the time of writing:

- `TODO(screenshots)`: README screenshots.
- `TODO(video-menu)`: README features, the in-game Video menu (video-settings branch).
- `TODO(min-macos)`: README requirements, the minimum macOS from `LSMinimumSystemVersion`.
- `TODO(release-link)`: README install, the real Releases URL / owner.
- `TODO(data-location)`: README install, where `BaseEF` goes for the .app (app-bundle branch).
- `TODO(verify-homepath)`: README saves, the config/save path if `HOMEPATH_NAME` changes.
- `TODO(verify-make-app)`: README build and reporting, `make-app.sh` usage and the bundle executable name.
- `TODO(credits-merge)`: CREDITS macOS section, describe the .app, video-settings and cleanup work.
- `TODO(verify-bundle)`: THIRD_PARTY_LICENSES, match the list to what the bundle actually contains
  (and switch the FFmpeg entry to LGPL if the minimal build is adopted).
- `.gitignore`: "make-app.sh output dir: TODO verify".
