# Third-party licenses

This file lists the third-party code that is **compiled into** the macOS engine, or **bundled as
shared libraries** in the macOS `.app`, with each one's license and where to find the full text.
Credits for the engine lineage (id Software, ioquake3, lilium-voyager, Quake3e) and for the Raven
game source are in [CREDITS.md](CREDITS.md). For the project's own licensing, see the License section
of the [README](README.md).

The versions below are the ones used for the first macOS builds (Homebrew, October 2026). Update them
when the release toolchain changes.
Checked against `dist/EliteForceSP.app/Contents/Frameworks` as produced by `macos/make-app.sh`
(8 dylibs: SDL3, sdl2-compat, Vulkan loader, MoltenVK and 4 FFmpeg libraries).
Re-check with `ls` / `otool -L` whenever the toolchain changes.

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
| FFmpeg (`libavformat`, `libavcodec`, `libavutil`, `libswscale`), minimal build | 9.0.2 | LGPL-2.1-or-later (see below) | <https://ffmpeg.org/legal.html>; source: <https://ffmpeg.org/releases/ffmpeg-9.0.2.tar.xz>; text: `Contents/Resources/licenses/FFmpeg-LGPL-2.1.txt` in the app |

The Apache-2.0 text is at <https://www.apache.org/licenses/LICENSE-2.0.txt>. Apache-2.0 also requires
redistributing any `NOTICE` files these projects ship.

### FFmpeg licensing

The app bundles a minimal FFmpeg built by [`macos/build-ffmpeg.sh`](macos/build-ffmpeg.sh) from the
unmodified FFmpeg 9.0.2 release tarball (SHA-256 pinned in the script). It is configured without
`--enable-gpl` / `--enable-version3` and with `--disable-autodetect --disable-everything`, enabling only
the `bink` demuxer, the `bink` / `binkaudio_rdft` / `binkaudio_dct` decoders and `swscale`, so it is
**LGPL-2.1-or-later** (the script refuses any other license) and links only against system libraries.

The LGPL obligations are met by:

- dynamic linking: the FFmpeg libraries are separate dylibs in `Contents/Frameworks` that users can
  replace with their own build of the same version;
- the LGPL text and a note naming the version, the source tarball and the build script, in
  `Contents/Resources/licenses/` inside the app;
- the exact configure options, in `macos/build-ffmpeg.sh` in this repository.

Homebrew's `ffmpeg` (GPL-3.0-or-later, plus x264, x265, OpenSSL and others) is no longer used by the
app; the dev build only falls back to it when `build-ffmpeg/install` does not exist.

## 4. Not included

- **Retail Elite Force data** (`*.pk3`, `*.bik` videos, retail executables and DLLs): proprietary.
  Not distributed; users supply their own.
- **RAD Game Tools Bink SDK**: not used. Bink playback goes through FFmpeg's decoder.
