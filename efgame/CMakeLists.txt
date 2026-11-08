cmake_minimum_required(VERSION 3.16)
project(efgame_sp CXX C)

# Elite Force single-player game modules (Raven STEF SP source) built as SHARED
# libraries the engine loads at runtime:
#   libefgame.so / .dylib / efgame.dll = game + cgame + icarus (+speedrun)
#                                        exports GetGameAPI, vmMain, dllEntry, CG_GetCameraPos, G_RegressDump
#   libefui.so   / .dylib / efui.dll   = ui, exports GetUIAPI
# An export list (GNU version script / ld64 exported-symbols list / PE .def generated from
# the .exp) exports ONLY those entry points and keeps everything else local (so the
# symbols game/ui share — Com_Printf etc — never collide, with each other or with the
# host engine). Mirrors retail efgamex86.dll / efuix86.dll.
#
# Built standalone (cmake -S efgame, as macos/make-app.sh does) or from the top-level
# CMakeLists.txt / CMakePresets.json (macos-arm64, windows-x64, linux-x64).
#
# Build (Android armv7):
#   cmake -B build -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=armeabi-v7a -DANDROID_PLATFORM=android-24
#   cmake --build build   -> build/libefgame.so, build/libefui.so

set(CMAKE_CXX_STANDARD 17)         # speedrun/ uses std::filesystem
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/EFPlatform.cmake)

set(SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(AND ${CMAKE_CURRENT_SOURCE_DIR}/android)

# ---- shared interface include dirs + compat shim ----
# On Windows the real <windows.h>/<direct.h>/<conio.h> are used: the android/ stand-ins
# stay off the include path (ef_android_compat.h itself is #if !defined(_WIN32)).
set(EF_INCLUDES)
if(NOT WIN32)
  list(APPEND EF_INCLUDES ${AND})   # compat shim (windows.h/direct.h stand-ins) — FIRST
endif()
list(APPEND EF_INCLUDES
  ${SRC}/game ${SRC}/cgame ${SRC}/ui ${SRC}/icarus
  ${SRC}/qcommon ${SRC}/client ${SRC}/renderer
  ${SRC}/speedrun ${SRC}/speedrun/strafe_helper ${SRC}/speedrun/overbounce_prediction
)
set(EF_COPTS
  # Optimize the native game/cgame/ICARUS code. This module is built via a bare
  # `cmake --build` with no CMAKE_BUILD_TYPE, so without this the toolchain emits
  # -O0 and the per-frame cgame draw + 20Hz game sim run unoptimized on the main
  # thread -> the renderer front end is CPU-starved -> low fps / stutter on
  # weaker devices. -fno-strict-aliasing: idTech3/Raven code type-puns freely.
  -O2
  -fno-strict-aliasing
  -include ${AND}/ef_android_compat.h   # (an empty header on Windows)
  ${EF_ERROR_LIMIT_FLAGS}   # clang -ferror-limit=0 / GCC -fmax-errors=0
  ${EF_NOWARN_FLAGS}        # clang -Wno-everything / GCC -w
  -fno-threadsafe-statics   # single-threaded modules; also dodges NDK r25 libc++abi
                            # cxa_guard ARM/Thumb-interworking link bug (syscall not STT_FUNC)
  -fcommon                  # MSVC/old-gcc merged tentative defs (e.g. `game_import_t gi;` in
                            # both g_main and gameinfo); modern clang is -fno-common by default
  # Strip the absolute host build path from __FILE__ / assert strings / debug info so
  # the developer's home directory never leaks into the shipped .so (or any crash log
  # a user sends back). -ffile-prefix-map covers debug info + __FILE__; -fmacro-prefix-map
  # covers __FILE__ used inside macros (assert). Maps this source root to a neutral token.
  -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=/src
  -fmacro-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=/src
  -DEF_DYLIB_MODULES        # macOS: don't take q_shared.h's OS X Server hard-link path
)
if(EF_COMPILER_GCC)
  # clang's -Wno-everything also silences the warnings-that-default-to-errors that the
  # Raven MSVC-era source trips (narrowing in aggregate initializers, ...); GCC needs them
  # downgraded explicitly.
  list(APPEND EF_COPTS $<$<COMPILE_LANGUAGE:CXX>:-fpermissive> $<$<COMPILE_LANGUAGE:CXX>:-Wno-narrowing>)
endif()
if(WIN32)
  list(APPEND EF_COPTS -DWIN32_LEAN_AND_MEAN -DNOMINMAX)
endif()

# ---- per-platform linking ----
# Windows: PE DLLs cannot have unresolved symbols, so --no-undefined is implicit; the
# exports come from a .def generated from the ld64 .exp list (minus Mach-O's leading "_").
# -static: libgcc / libstdc++ / winpthread linked in, so no mingw runtime DLLs ship.
option(EF_NO_UNDEFINED "Fail the module link on unresolved symbols (Linux: instead of ignore-all)" OFF)
function(ef_module_link target exp ver)
  if(APPLE)
    # macOS ld64: exported-symbols list stands in for the GNU version script;
    # dynamic_lookup stands in for --unresolved-symbols=ignore-all.
    target_link_options(${target} PRIVATE
      -Wl,-exported_symbols_list,${AND}/${exp} -undefined dynamic_lookup)
    # Relink when the export list changes (ld64 does not track it as an input).
    set_target_properties(${target} PROPERTIES LINK_DEPENDS ${AND}/${exp})
  elseif(WIN32)
    file(STRINGS ${AND}/${exp} _syms)
    set(_def "LIBRARY ${target}\nEXPORTS\n")
    foreach(_s IN LISTS _syms)
      string(STRIP "${_s}" _s)
      if(_s)
        # drop Mach-O's one leading "_" (not REGEX REPLACE "^_": it re-anchors and strips all)
        string(SUBSTRING "${_s}" 0 1 _c)
        if(_c STREQUAL "_")
          string(SUBSTRING "${_s}" 1 -1 _s)
        endif()
        string(APPEND _def "  ${_s}\n")
      endif()
    endforeach()
    set(_deffile ${CMAKE_CURRENT_BINARY_DIR}/${target}.def)
    file(CONFIGURE OUTPUT ${_deffile} CONTENT "${_def}" @ONLY)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${AND}/${exp})
    target_sources(${target} PRIVATE ${_deffile})
    set_target_properties(${target} PROPERTIES PREFIX "" IMPORT_PREFIX "" OUTPUT_NAME ${target})
    target_link_options(${target} PRIVATE -static -Wl,--no-undefined)
  else()
    if(EF_NO_UNDEFINED)
      target_link_options(${target} PRIVATE -Wl,--version-script=${AND}/${ver} -Wl,--no-undefined)
    else()
      target_link_options(${target} PRIVATE
        -Wl,--version-script=${AND}/${ver}
        -Wl,--unresolved-symbols=ignore-all)   # engine syscalls resolved at dlopen; libc/libm linked below
    endif()
    if(ANDROID)
      target_link_options(${target} PRIVATE
        -Wl,-z,max-page-size=16384   # 16 KB page-size alignment (Android 15+); NDK r25 defaults to 4 KB
        # NDK r25 adds -Wl,--fatal-warnings by default; on armeabi-v7a the prebuilt
        # libc++abi cxa_guard.o emits a benign ARM/Thumb interworking warning that
        # then fails the link. Override (last-one-wins) so armv7 links cleanly.
        -Wl,--no-fatal-warnings)
    endif()
    set_target_properties(${target} PROPERTIES LINK_DEPENDS ${AND}/${ver})
  endif()
endfunction()

# ---- shared utility sources (compiled into BOTH modules, like the retail projects) ----
# q_math/q_shared/q_color etc. live in game/ but the ui module needs them too. Discover by
# pattern: the q_*.cpp shared core (NOT bg_lib = QVM libc).
file(GLOB EF_SHARED_SRC ${SRC}/game/q_math.cpp ${SRC}/game/q_shared.cpp ${SRC}/game/q_color.cpp)

# ===================== libefgame.so (game + cgame + icarus) =====================
file(GLOB_RECURSE EFGAME_SRC
  ${SRC}/game/*.cpp ${SRC}/cgame/*.cpp ${SRC}/icarus/*.cpp
  ${SRC}/speedrun/*.cpp ${SRC}/speedrun/*.c ${SRC}/speedrun/strafe_helper/*.c)
# gameinfo.cpp lives in ui/ but is a SHARED source (defines weaponData/ammoData tables);
# retail game.vcxproj compiles ..\ui\gameinfo.cpp too.
list(APPEND EFGAME_SRC ${SRC}/ui/gameinfo.cpp)
list(REMOVE_ITEM EFGAME_SRC ${SRC}/game/bg_lib.cpp)   # QVM freestanding libc — native uses platform libc
# The retail game project (game.vcxproj) compiles only these ICARUS files. Interface.cpp is a
# stub (real Interface_Init lives in game/Q3_Interface.cpp); Interpreter.cpp + Tokenizer.cpp are
# the offline BehavEd script compiler (in-game loads precompiled .ibi). Excluding them.
list(REMOVE_ITEM EFGAME_SRC
  ${SRC}/icarus/Interface.cpp ${SRC}/icarus/Interpreter.cpp ${SRC}/icarus/Tokenizer.cpp)

# Co-op (macOS test builds only, default OFF): the sources live in src/coop/, outside the globs
# above, so an OFF build (and the Android build) never compiles them. With it OFF,
# game/coop_public.h turns every co-op helper back into the expression it replaced.
option(EF_COOP "Single-player co-op (experimental)" OFF)
if(EF_COOP)
  file(GLOB EFCOOP_SRC ${SRC}/coop/*.cpp)
  list(APPEND EFGAME_SRC ${EFCOOP_SRC})
endif()

add_library(efgame SHARED ${EFGAME_SRC})
target_include_directories(efgame PRIVATE ${EF_INCLUDES})
target_compile_options(efgame PRIVATE ${EF_COPTS})
target_compile_definitions(efgame PRIVATE NDEBUG)
if(EF_COOP)
  target_compile_definitions(efgame PRIVATE EF_COOP)
  if(APPLE)
    # the engine's SP bridge hands the co-op code its connection (coop_net.cpp, coop_bridge.h)
    target_link_options(efgame PRIVATE -Wl,-exported_symbol,_COOP_SetBridge -Wl,-exported_symbol,_COOP_GetReplica)
  endif()
endif()
ef_module_link(efgame efgame.exp efgame.ver)
target_link_libraries(efgame m)

# ===================== libefui.so (ui) =====================
file(GLOB EFUI_SRC ${SRC}/ui/*.cpp)
# Retail ui.vcxproj compiles ..\game\g_weaponLoad.cpp with _USRDLL;UI_EXPORTS so its
# WP_LoadWeaponParms() populates weaponData/ammoData on the UI side (loadout/weapon menus).
# Under _USRDLL the FX_*Think func-table (the #else / #ifndef _USRDLL branch) is excluded ->
# zero cgame deps; the only symbol it then needs is gameinfo's `gi` (see gameinfo.cpp guard).
list(APPEND EFUI_SRC ${SRC}/game/g_weaponLoad.cpp)
add_library(efui SHARED ${EFUI_SRC} ${EF_SHARED_SRC} ${EFUI_EXTRA_SRC})
target_include_directories(efui PRIVATE ${EF_INCLUDES})
target_compile_options(efui PRIVATE ${EF_COPTS})
# _USRDLL must be target-wide (not per-file) so the globbed gameinfo.cpp also takes the UI branch.
# Do NOT add _USRDLL to the efgame target (the game build keeps g_weaponLoad's cgame funcs[] table).
target_compile_definitions(efui PRIVATE NDEBUG _USRDLL UI_EXPORTS)
ef_module_link(efui efui.exp efui.ver)
target_link_libraries(efui m)
