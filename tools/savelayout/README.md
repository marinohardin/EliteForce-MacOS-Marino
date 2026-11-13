# Savegame layout tools

`gen_savetranscode.py` regenerates `efgame/src/game/g_savetranscode.cpp`, the table that
lets 64-bit builds read and write savegames in the retail 32-bit layout.

## Background

The game saves four chunks as raw struct dumps: `GENT` (gentity_t), `GCLI` (gclient_t),
`GNPC` (gNPC_t) and `LVLC` (level_locals_t up to `logFile`). Retail PC (MSVC i386) and
armv7 wrote them in the ILP32 layout. A 64-bit build has 8-byte pointers and different
padding, so `g_savetranscode.cpp` holds one copy record `{native offset, disk offset, nbytes}`
per scalar leaf (pointer fields copy their low 4 bytes, which hold an index at save time),
plus `static_assert`s on `sizeof` and on the offset of every direct member.

The transcoder is on when `EF_SAVE_TRANSCODE` is defined (`g_savetranscode.h`):
`__LP64__` (macOS and Linux, arm64 and x86_64) or `_WIN64` (LLP64). The saved structs hold no
`long`, `size_t`, `double` or bit-fields, so all three native ABIs have identical layouts and
share one table. The generator would emit separate `#if` sections if they ever differed.

| chunk | native size (aarch64-darwin = x86_64-linux = x86_64-mingw) | disk size (i686) | records |
|------|------|------|------|
| GENT | 1320 | 1056 | 264 |
| GCLI | 3416 | 3364 | 993 |
| GNPC | 680  | 612  | 156 |
| LVLC | 1644 (offsetof logFile) | 1508 | 425 |

## Inputs

`layouts/<triple>.json` are filtered `clang -Xclang -fdump-record-layouts-canonical` dumps of a
TU that includes `g_local.h`/`g_shared.h` with efgame's flags (`-include ef_android_compat.h`,
`-DEF_DYLIB_MODULES`, include dirs; on mingw targets the `efgame/android` shim dir is left
off the include path so the real `<windows.h>` is used). Disk layout = `i686-w64-mingw32`
(MSVC record layout rules). Typedefs of anonymous enums (team_t, ...) are checked with
`__is_enum` and sized by a probe compile.

## Commands

Run from the repo root (needs clang with `-fdump-record-layouts-canonical`, which clang 11 lacks;
Apple clang works).

```sh
# 1. dump layouts (Homebrew mingw-w64 is used as the Windows sysroot; override with MINGW_PREFIX)
python3 tools/savelayout/gen_savetranscode.py dump i686-w64-mingw32 aarch64-apple-darwin x86_64-w64-mingw32

# Linux x86_64 needs glibc + libstdc++ headers. Copy them out of the Steam Runtime sniper SDK
# image (clang 11 in the image is too old for the canonical dump):
mkdir -p tools/savelayout/build/sysroot-x86_64-linux/usr/lib/gcc/x86_64-linux-gnu
docker run --rm --platform linux/amd64 -v "$PWD/tools/savelayout/build:/out" \
  registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest \
  cp -a /usr/lib/gcc/x86_64-linux-gnu/10 /out/sysroot-x86_64-linux/usr/lib/gcc/x86_64-linux-gnu/
docker run --rm --platform linux/amd64 -v "$PWD/tools/savelayout/build:/out" \
  registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest cp -a /usr/include /out/sysroot-x86_64-linux/usr/
# (on a case-insensitive macOS volume this cp reports clashes in unrelated FL/netfilter headers; harmless)
python3 tools/savelayout/gen_savetranscode.py dump x86_64-linux-gnu --sysroot tools/savelayout/build/sysroot-x86_64-linux

# 2. regenerate efgame/src/game/g_savetranscode.cpp (or: check, exit 1 if stale)
python3 tools/savelayout/gen_savetranscode.py gen

# 3. syntax-check the transcoder + g_savegame.cpp per 64-bit target (evaluates the static_asserts)
python3 tools/savelayout/gen_savetranscode.py verify \
  --linux-sysroot tools/savelayout/build/sysroot-x86_64-linux \
  --mingw-gxx "$(command -v x86_64-w64-mingw32-g++)"

# print just the data tables for one ABI (to diff two ABIs, or against an old file)
python3 tools/savelayout/gen_savetranscode.py tables x86_64-w64-mingw32
```

Rerun 1 and 2 after changing any field of the four structs (or anything nested in them). A
change to the saved structs also changes the retail save format, so it is almost always wrong.

## History

The original generator (and its `save_struct_layouts.json`) was lost. This one reproduces the
LP64 file's data tables (records, descriptor sizes, self-check) byte for byte. It asserts the
offset of every direct member, where the old file asserted all of GENT's but only a few of the
other structs', and it adds `static_assert`s on the native sizes.
