# Third-party licenses

This file lists the third-party code that is **compiled into** the macOS engine, or **bundled as
shared libraries** in the macOS `.app`, with each one's license and where to find the full text.
Credits for the engine lineage (id Software, ioquake3, lilium-voyager, Quake3e) and for the Raven
game source are in [CREDITS.md](CREDITS.md). For the project's own licensing, see the License section
of the [README](README.md).

The versions below are the ones used for the first macOS builds (Homebrew, October 2026). Update them
when the release toolchain changes.
<!-- TODO(verify-bundle): reconcile this list with what macos/make-app.sh actually copies into
     Contents/Frameworks (run `otool -L` on every bundled binary). -->

## 1. Project code

| Component | Path | License | Text |
|---|---|---|---|
| Engine (Quake III Arena → ioquake3 → lilium-voyager; Quake3e `renderervk`) plus the Android and macOS port code | `EFAndroid-SP/app/jni/efcode`, `EFAndroid-SP/app/jni/sp`, `EFAndroid-SP/app/jni/macos` | GPL-2.0-or-later | [LICENSE](LICENSE) |
| Elite Force SP game/UI modules (`libefgame.dylib`, `libefui.dylib`) | `efgame/` | Raven Software Limited Use Software License Agreement (non-commercial, free distribution only, for use with the retail game). **Not open source.** | `efgame/STEF Game Source License.doc` |

## 2. Libraries compiled into the engine executable (in-tree sources)

These are the only in-tree third-party libraries the macOS build compiles
(see `EFAndroid-SP/app/jni/macos/CMakeLists.txt`):

| Component | Version | Path | License | Text |
|---|---|---|---|---|
| zlib | (in-tree copy) | `efcode/zlib` | zlib | license header in `efcode/zlib/zlib.h`; <https://zlib.net/zlib_license.html> |
| libjpeg (Independent JPEG Group) | 8c | `efcode/jpeg-8c` | IJG license (BSD-like, requires acknowledgement) | "LEGAL ISSUES" in `efcode/jpeg-8c/README` |
| libmad (Underbit Technologies) | 0.15.1b | `efcode/libmad-0.15.1b` | GPL-2.0-or-later | `efcode/libmad-0.15.1b/COPYRIGHT` (GPL text: [LICENSE](LICENSE)) |

"This software is based in part on the work of the Independent JPEG Group." (required by the IJG
license.)

The tree also contains `libogg`, `libvorbis`, `opus`, `opusfile`, `curl`, `SDL2` sources, and the LCC
QVM compiler (`efcode/tools/lcc`). The macOS build does **not** compile any of them. OpenAL is optional
and loaded at runtime from the system `OpenAL.framework`; it is not bundled.

## 3. Shared libraries bundled in the macOS `.app`

| Component | Version | License | License text / source |
|---|---|---|---|
| SDL 3 | 3.4.18 | Zlib | <https://github.com/libsdl-org/SDL/blob/main/LICENSE.txt> |
| sdl2-compat (SDL2 API on SDL3) | 2.32.74 | Zlib | <https://github.com/libsdl-org/sdl2-compat/blob/main/LICENSE.txt> |
| Vulkan-Loader (Khronos) | 1.4.363 | Apache-2.0 | <https://github.com/KhronosGroup/Vulkan-Loader/blob/main/LICENSE.txt> |
| MoltenVK (Khronos / Brenwill) | 1.4.2 | Apache-2.0 | <https://github.com/KhronosGroup/MoltenVK/blob/main/LICENSE> |
| FFmpeg (`libavformat`, `libavcodec`, `libavutil`, `libswscale`, `libswresample`) | 9.0.2 | **GPL-3.0-or-later** (see below) | <https://ffmpeg.org/legal.html>; source: <https://ffmpeg.org/releases/>; GPLv3 text: <https://www.gnu.org/licenses/gpl-3.0.txt> |

The Apache-2.0 text is at <https://www.apache.org/licenses/LICENSE-2.0.txt>. Apache-2.0 also requires
redistributing any `NOTICE` files these projects ship.

### FFmpeg licensing (important for redistribution)

FFmpeg itself is LGPL-2.1-or-later **unless** it is configured with GPL or version-3 options.
Homebrew's `ffmpeg` 9.0.2 bottle is configured with:

```
--enable-gpl --enable-version3 --enable-libx264 --enable-libx265 --enable-libsvtav1
--enable-libdav1d --enable-libvpx --enable-libopus --enable-libmp3lame --enable-libvmaf
--enable-openssl --enable-videotoolbox --enable-audiotoolbox ...
```

Homebrew labels the result **GPL-3.0-or-later**. Bundling it as-is means:

- the `.app` contains GPLv3 code, so the combined engine binary has to be distributed under GPLv3 terms
  (the engine is GPL-2.0-*or-later*, so that is compatible);
- the corresponding source for FFmpeg and its libraries must be offered with the binaries;
- the FFmpeg dylibs pull in these additional Homebrew libraries, which must be bundled and listed too:

| Dependency (pulled in by FFmpeg) | Version | License |
|---|---|---|
| x264 | r3108 | GPL-2.0-or-later |
| x265 | 4.1 | GPL-2.0-or-later |
| SVT-AV1 | 2.3.0 | BSD-3-Clause (+ AOM patent license) |
| dav1d | 1.5.1 | BSD-2-Clause |
| libvpx | 1.15.0 | BSD-3-Clause |
| Opus | 1.5.2 | BSD-3-Clause |
| LAME (`libmp3lame`) | 3.100 | LGPL-2.0-or-later |
| mpg123 (via LAME) | 1.32.10 | LGPL-2.1-only |
| OpenSSL (`libssl`, `libcrypto`) | 4.0.3 | Apache-2.0 |
| xz (`liblzma`) | 5.8.4 | 0BSD (liblzma) |

**Recommendation:** the game only *decodes* Bink video, so a minimal FFmpeg build is enough. Build
it LGPL-only, with no `--enable-gpl` or `--enable-version3` and no external libraries, enabling only
the `bink`/`binkaudio_*` decoders, the `bink` demuxer, `swscale` and `swresample`. That gives a small,
LGPL-2.1 FFmpeg with no x264/x265/OpenSSL baggage. See [docs/PUBLISHING.md](docs/PUBLISHING.md).

## 4. Not included

- **Retail Elite Force data** (`*.pk3`, `*.bik` videos, retail executables and DLLs): proprietary.
  Not distributed; users supply their own.
- **RAD Game Tools Bink SDK**: not used. Bink playback goes through FFmpeg's decoder.
