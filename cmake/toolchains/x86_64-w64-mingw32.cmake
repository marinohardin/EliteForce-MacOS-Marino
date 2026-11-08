# Cross toolchain: Windows x64 (x86_64-w64-mingw32), from macOS or Linux.
#
# Works with:
#   - Homebrew mingw-w64        (brew install mingw-w64: GCC, x86_64-w64-mingw32-gcc in /opt/homebrew/bin)
#   - Ubuntu/Debian mingw-w64   (apt install mingw-w64: x86_64-w64-mingw32-gcc-posix, -win32 variants)
#   - llvm-mingw                (clang; x86_64-w64-mingw32-clang)
# Both GCC and clang targeting mingw use Itanium C++ mangling, which the engine relies on
# (it looks up _Z10GetGameAPIP13game_import_t etc. in efgame.dll / efui.dll).
#
# Select the compiler family with -DEF_MINGW_COMPILER=gcc|clang (default: gcc if found, else
# clang), and an explicit toolchain location with -DEF_MINGW_ROOT=<dir containing bin/>.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(_triple x86_64-w64-mingw32)
set(EF_MINGW_COMPILER "" CACHE STRING "mingw compiler family: gcc or clang (empty = auto)")
set(EF_MINGW_ROOT "" CACHE PATH "mingw toolchain root (its bin/ holds ${_triple}-*); empty = PATH")

set(_hints)
if(EF_MINGW_ROOT)
  list(APPEND _hints "${EF_MINGW_ROOT}/bin")
endif()
list(APPEND _hints /opt/homebrew/bin /usr/local/bin /usr/bin /opt/llvm-mingw/bin)

if(NOT EF_MINGW_COMPILER OR EF_MINGW_COMPILER STREQUAL "gcc")
  # Ubuntu ships -posix (winpthreads, std::thread) and -win32 variants; prefer -posix.
  find_program(_ef_mingw_cc  NAMES ${_triple}-gcc-posix ${_triple}-gcc HINTS ${_hints} NO_CACHE)
  find_program(_ef_mingw_cxx NAMES ${_triple}-g++-posix ${_triple}-g++ HINTS ${_hints} NO_CACHE)
endif()
if((NOT _ef_mingw_cc OR NOT _ef_mingw_cxx) AND NOT EF_MINGW_COMPILER STREQUAL "gcc")
  find_program(_ef_mingw_cc  NAMES ${_triple}-clang   HINTS ${_hints} NO_CACHE)
  find_program(_ef_mingw_cxx NAMES ${_triple}-clang++ HINTS ${_hints} NO_CACHE)
endif()
if(NOT _ef_mingw_cc OR NOT _ef_mingw_cxx)
  message(FATAL_ERROR "No ${_triple} compiler found (install mingw-w64 or llvm-mingw, or set EF_MINGW_ROOT)")
endif()

set(CMAKE_C_COMPILER   "${_ef_mingw_cc}")
set(CMAKE_CXX_COMPILER "${_ef_mingw_cxx}")
find_program(CMAKE_RC_COMPILER NAMES ${_triple}-windres llvm-windres HINTS ${_hints})
find_program(CMAKE_AR     NAMES ${_triple}-gcc-ar ${_triple}-ar llvm-ar HINTS ${_hints})
find_program(CMAKE_RANLIB NAMES ${_triple}-gcc-ranlib ${_triple}-ranlib llvm-ranlib HINTS ${_hints})
find_program(CMAKE_OBJDUMP NAMES ${_triple}-objdump llvm-objdump HINTS ${_hints})

# Target sysroot first. Libraries/headers are BOTH (not ONLY) so the absolute paths that
# pkg-config reports for the deps prefix (build-deps/windows/install) are not re-rooted.
get_filename_component(_bindir "${_ef_mingw_cc}" DIRECTORY)
set(CMAKE_FIND_ROOT_PATH "${_bindir}/../${_triple}" "${_bindir}/../toolchain/${_triple}" /usr/${_triple})
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# pkg-config: a triple-prefixed wrapper if the distro has one, else the host's pkg-config
# (the deps prefix is put on PKG_CONFIG_LIBDIR by cmake/EFPlatform.cmake).
find_program(PKG_CONFIG_EXECUTABLE NAMES ${_triple}-pkg-config pkg-config pkgconf HINTS ${_hints})
