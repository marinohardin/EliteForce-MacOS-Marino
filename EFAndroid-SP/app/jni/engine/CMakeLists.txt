cmake_minimum_required(VERSION 3.16)

# ---------------------------------------------------------------------------
# Elite Force engine (single player + Holomatch): macOS arm64, Windows x64, Linux x86_64.
# Mirrors ../src/CMakeLists.txt (the Android libmain.so build) but produces an executable,
# uses SDL2 (sdl2-compat on SDL3) and FFmpeg from pkg-config, and loads Vulkan at runtime
# through SDL_Vulkan_LoadLibrary (MoltenVK on macOS, the system loader elsewhere).
#
#   standalone:  cmake -S EFAndroid-SP/app/jni/engine -B build-engine && cmake --build build-engine
#   macOS:       cmake -S EFAndroid-SP/app/jni/macos ...   (thin wrapper, used by macos/make-app.sh)
#   everything:  cmake --preset <macos-arm64|windows-x64|linux-x64>   (top-level CMakeLists.txt)
#   -> <build>/efsp[.exe]  (put the efgame / efui modules next to it)
# ---------------------------------------------------------------------------

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(efsp C CXX)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cmake/EFPlatform.cmake)
if(APPLE)
  enable_language(OBJC)
endif()
if(WIN32)
  enable_language(RC)
endif()
include(CheckCCompilerFlag)

# the app's version (make-app.sh passes its VERSION), shown as "Elite Force for Mac <version>"
set(EF_APP_VERSION "1.3.0" CACHE STRING "App version shown in the console and to server browsers")
option(EF_BINK "Bink cinematics via FFmpeg (OFF = stub, no movies)" ON)
option(EF_VM_JIT "QVM JIT (aarch64: needs MAP_JIT work on macOS; x86_64: vm_x86.c, untested)" OFF)
# single-player co-op (experimental): the engine side is the Mac launcher's Co-op… sheet, the
# host's answer to co-op checks and the co-op connection (sys/sys_osx_coop.m, sys_coop_net.c;
# macOS only for now); the game side is efgame's option of the same name (make-app.sh passes both).
option(EF_COOP "Single-player co-op (experimental)" OFF)
# GOG installer extraction (sys_osx_gog.c + liblzma). On macOS the Mac launcher calls it; on
# Windows/Linux the launcher core (efcode/launcher, target eflauncher) compiles it and links liblzma.
if(APPLE)
  option(EF_GOG "GOG offline-installer extraction (sys_osx_gog.c, liblzma)" ON)
else()
  option(EF_GOG "GOG offline-installer extraction in the launcher core (sys_osx_gog.c, liblzma)" ON)
endif()
if(WIN32)
  option(EF_WIN_GUI "Windows GUI-subsystem executable (-mwindows, like ioq3's client)" ON)
  # the in-process Win32 launcher (efcode/sys/win32/, Sys_WinLauncher); off until its sources exist
  option(EF_WIN_LAUNCHER "Windows: the in-process Win32 launcher (Sys_WinLauncher)" OFF)
endif()
# where efsp lands: the top of the build tree, so the top-level build puts it next to the
# game modules and macos/make-app.sh finds it at $ENGINE_BUILD/efsp as before
set(EF_ENGINE_OUTPUT_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Output directory for efsp")


set(JNI ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(EF  ${JNI}/efcode)
set(SP  ${JNI}/sp)
set(EFG ${JNI}/../../../efgame)
# desktop stand-in for <android/log.h> (android/log.h) and bink_stub.c; a shared compat/
# directory wins if the sp/ work has moved the shim there
if(EXISTS ${JNI}/compat/android/log.h)
  set(EF_COMPAT_DIR ${JNI}/compat)
else()
  set(EF_COMPAT_DIR ${JNI}/macos)
endif()
if(EXISTS ${EF_COMPAT_DIR}/bink_stub.c)
  set(EF_BINK_STUB ${EF_COMPAT_DIR}/bink_stub.c)
else()
  set(EF_BINK_STUB ${JNI}/macos/bink_stub.c)
endif()

message(STATUS "Elite Force engine: ${EF_PLATFORM} ${EF_ARCH} (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})")

# ---- dependencies (pkg-config) ----
find_package(PkgConfig REQUIRED)
# SDL2 (sdl2-compat on SDL3) from the platform's build-deps script when present
# (macOS: build-deps/install, else build-deps/<platform>/install); else pkg-config's default
ef_default_prefix(_ef_deps_default build-deps lib/pkgconfig/sdl2.pc)
ef_use_deps_prefix(EF_DEPS_PREFIX "${_ef_deps_default}" "SDL2/SDL3(/Vulkan loader) prefix from the build-deps script (empty = pkg-config default)")
unset(__pkg_config_checked_SDL2 CACHE)
unset(pkgcfg_lib_SDL2_SDL2 CACHE)
unset(pkgcfg_lib_SDL2_SDL2main CACHE)
if(APPLE)
  pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
else()
  pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
endif()
if(SDL2_FOUND)
  message(STATUS "SDL2: ${SDL2_LINK_LIBRARIES}")
  set(EF_SDL_TARGET PkgConfig::SDL2)
else()
  # No SDL2 build for the target yet: compile against the bundled SDL 2.24 headers
  # (efcode/SDL2/include) and link a library named by EF_SDL2_LIBRARY if given. Good for
  # finding compile errors; a runnable build needs build-deps/<platform>/install.
  set(EF_SDL2_LIBRARY "" CACHE FILEPATH "SDL2 library/import lib to link when pkg-config has no sdl2")
  message(WARNING "pkg-config found no sdl2 for ${EF_PLATFORM}: using efcode/SDL2/include headers"
    " and EF_SDL2_LIBRARY='${EF_SDL2_LIBRARY}' (efsp will not link without a library)")
  add_library(ef_sdl2_local INTERFACE)
  target_include_directories(ef_sdl2_local INTERFACE ${EF}/SDL2/include)
  if(EF_SDL2_LIBRARY)
    target_link_libraries(ef_sdl2_local INTERFACE ${EF_SDL2_LIBRARY})
  endif()
  if(WIN32)
    # no SDL2main import lib to pair with the bundled headers: sys_main.c's main() is
    # the entry point itself (mingw's crt calls main() for GUI executables too)
    target_compile_definitions(ef_sdl2_local INTERFACE SDL_MAIN_HANDLED)
  endif()
  set(EF_SDL_TARGET ef_sdl2_local)
endif()
if(EF_BINK)
  # Prefer the minimal LGPL FFmpeg from the build-ffmpeg script over Homebrew's
  # (GPL-3.0) one; make-app.sh bundles whichever was linked.
  ef_default_prefix(_ef_ffmpeg_default build-ffmpeg lib/pkgconfig/libavcodec.pc)
  ef_use_deps_prefix(EF_FFMPEG_PREFIX "${_ef_ffmpeg_default}" "FFmpeg install prefix (empty = pkg-config default, e.g. Homebrew)")
  # re-resolve on every configure, so a changed prefix takes effect
  unset(__pkg_config_checked_FFMPEG CACHE)
  foreach(_lib avformat avcodec swscale avutil)
    unset(pkgcfg_lib_FFMPEG_${_lib} CACHE)
  endforeach()
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
  message(STATUS "FFmpeg: ${FFMPEG_LINK_LIBRARIES}")
endif()

# ---- sources ----
set(CLIENT_SRC
  ${EF}/client/cl_cgame.c   ${EF}/client/cl_cin.c     ${EF}/client/cl_console.c
  ${EF}/client/cl_input.c   ${EF}/client/cl_keys.c    ${EF}/client/cl_main.c
  ${EF}/client/cl_net_chan.c ${EF}/client/cl_parse.c  ${EF}/client/cl_scrn.c
  ${EF}/client/cl_ui.c      ${EF}/client/cl_avi.c     ${EF}/client/cl_curl.c
  ${EF}/client/snd_adpcm.c  ${EF}/client/snd_dma.c    ${EF}/client/snd_mem.c
  ${EF}/client/snd_mix.c    ${EF}/client/snd_wavelet.c
  ${EF}/client/snd_main.c   ${EF}/client/snd_codec.c  ${EF}/client/snd_codec_wav.c
  ${EF}/client/snd_codec_mp3.c
  ${EF}/client/qal.c        ${EF}/client/snd_openal.c)

file(GLOB LIBMAD_SRC ${EF}/libmad-0.15.1b/*.c)

set(QCOMMON_SRC
  ${EF}/qcommon/cm_load.c   ${EF}/qcommon/cm_patch.c  ${EF}/qcommon/cm_polylib.c
  ${EF}/qcommon/cm_test.c   ${EF}/qcommon/cm_trace.c
  ${EF}/qcommon/cmd.c       ${EF}/qcommon/common.c    ${EF}/qcommon/cvar.c
  ${EF}/qcommon/files.c     ${EF}/qcommon/md4.c       ${EF}/qcommon/md5.c
  ${EF}/qcommon/msg.c       ${EF}/qcommon/net_chan.c  ${EF}/qcommon/net_ip.c
  ${EF}/qcommon/huffman.c   ${EF}/qcommon/q_math.c    ${EF}/qcommon/q_shared.c
  ${EF}/qcommon/unzip.c     ${EF}/qcommon/ioapi.c     ${EF}/qcommon/puff.c
  ${EF}/qcommon/vm.c        ${EF}/qcommon/vm_interpreted.c
  ${EF}/qcommon/regress.c)
if(EF_VM_JIT)
  if(EF_ARCH STREQUAL "arm64")
    list(APPEND QCOMMON_SRC ${EF}/qcommon/vm_aarch64.c)
  elseif(EF_ARCH STREQUAL "x86_64")
    list(APPEND QCOMMON_SRC ${EF}/qcommon/vm_x86.c)
  else()
    message(FATAL_ERROR "EF_VM_JIT: no JIT for ${EF_ARCH}")
  endif()
endif()
# q_shared.h maps Q_ftol / Q_SnapVector to the SSE versions when idx64 (x86_64)
if(EF_ARCH STREQUAL "x86_64")
  list(APPEND QCOMMON_SRC ${EF}/asm/ftola.c ${EF}/asm/snapvector.c)
endif()

set(SERVER_SRC
  ${EF}/server/sv_bot.c     ${EF}/server/sv_ccmds.c   ${EF}/server/sv_client.c
  ${EF}/server/sv_game.c    ${EF}/server/sv_init.c    ${EF}/server/sv_main.c
  ${EF}/server/sv_net_chan.c ${EF}/server/sv_snapshot.c ${EF}/server/sv_world.c)

file(GLOB BOTLIB_SRC ${EF}/botlib/*.c)

set(SDL_SRC ${EF}/sdl/sdl_input.c ${EF}/sdl/sdl_snd.c ${EF}/sdl/sdl_gamma.c ${EF}/sdl/sdl_glimp.c)

# sys layer per platform
if(APPLE)
  # lilium-voyager's macOS sys layer: tty console + Cocoa dialogs/launcher (sys_osx*.m).
  set(SYS_SRC
    ${EF}/sys/con_log.c ${EF}/sys/con_tty.c ${EF}/sys/sys_autoupdater.c
    ${EF}/sys/sys_unix.c ${EF}/sys/sys_main.c ${EF}/sys/sys_osx.m
    ${EF}/sys/sys_osx_data.m ${EF}/sys/sys_osx_launcher.m ${EF}/sys/sys_osx_server.m)
  if(EF_GOG)
    list(APPEND SYS_SRC ${EF}/sys/sys_osx_gog.c)
  endif()
  set_source_files_properties(${EF}/sys/sys_osx_launcher.m ${EF}/sys/sys_osx_server.m
    PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  if(EF_COOP)
    list(APPEND SYS_SRC ${EF}/sys/sys_osx_coop.m ${EF}/sys/sys_coop_net.c)
    set_source_files_properties(${EF}/sys/sys_osx_coop.m PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  endif()
elseif(WIN32)
  set(SYS_SRC
    ${EF}/sys/sys_win32.c ${EF}/sys/con_win32.c ${EF}/sys/con_log.c ${EF}/sys/sys_main.c
    ${EF}/sys/sys_autoupdater.c ${EF}/sys/sys_platform_desktop.c ${EF}/sys/sys_data.c
    ${EF}/sys/win_resource.rc)
  # win_resource.rc: its manifest is next to it; under mingw it wants the icon at
  # "misc/lilium.ico", which cmake/windows/misc/lilium.ico provides via the include path
  # windres resolves relative paths from its working directory, so the icon path is absolute
  set_source_files_properties(${EF}/sys/win_resource.rc PROPERTIES
    INCLUDE_DIRECTORIES "${EF}/sys;${EF_ROOT}/cmake/windows"
    COMPILE_DEFINITIONS "EF_WIN_ICON=\\\"${EF_ROOT}/windows/icon.ico\\\"")
else()
  set(SYS_SRC
    ${EF}/sys/sys_unix.c ${EF}/sys/con_tty.c ${EF}/sys/con_log.c ${EF}/sys/sys_main.c
    ${EF}/sys/sys_autoupdater.c ${EF}/sys/sys_platform_desktop.c ${EF}/sys/sys_data.c)
endif()

file(GLOB RENDERCOMMON_SRC ${EF}/renderercommon/*.c)
file(GLOB RENDERER_SRC ${EF}/renderervk/*.c)
list(REMOVE_ITEM RENDERER_SRC ${EF}/renderervk/tr_subs.c)
list(REMOVE_ITEM RENDERER_SRC ${EF}/renderervk/shaders/spirv/shader_data.c)

file(GLOB ZLIB_SRC ${EF}/zlib/*.c)
file(GLOB JPEG_SRC ${EF}/jpeg-8c/*.c)

if(EF_BINK)
  set(BINK_SRC ${SP}/bink_ff.c)
else()
  set(BINK_SRC ${EF_BINK_STUB})
endif()

# ---- compiler flags ----
# Apple clang 17+ / GCC 14+ error on these by default; NDK r25 clang only warned.
set(EF_ENGINE_COPTS -fno-strict-aliasing -Werror=implicit-function-declaration -O2 -w)
if(EF_COMPILER_CLANG)
  list(APPEND EF_ENGINE_COPTS
    -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion)
else()
  foreach(_f incompatible-pointer-types int-conversion implicit-int return-mismatch)
    string(MAKE_C_IDENTIFIER "EF_HAVE_WNO_ERROR_${_f}" _var)
    check_c_compiler_flag(-Wno-error=${_f} ${_var})
    if(${_var})
      list(APPEND EF_ENGINE_COPTS $<$<COMPILE_LANGUAGE:C>:-Wno-error=${_f}>)
    endif()
  endforeach()
endif()

# ---- SP game/cgame bridge (C++ against the SP game headers) ----
add_library(sp_game_bridge STATIC
  ${SP}/sp_bridge.cpp ${SP}/sp_ui_bridge.cpp ${SP}/sp_clink.c)
target_include_directories(sp_game_bridge PRIVATE ${EF_COMPAT_DIR})   # android/log.h shim
if(NOT WIN32)
  # the efgame compat shim (windows.h stand-in); Windows uses the real headers
  target_include_directories(sp_game_bridge PRIVATE ${EFG}/android)
endif()
target_include_directories(sp_game_bridge PRIVATE
  ${EFG}/src/game ${EFG}/src/cgame ${EFG}/src/ui
  ${EFG}/src/client ${EFG}/src/renderer ${EFG}/src/qcommon ${SP})
target_compile_options(sp_game_bridge PRIVATE
  -include ${EFG}/android/ef_android_compat.h -fno-threadsafe-statics -fcommon -w)
if(EF_COMPILER_GCC)
  target_compile_options(sp_game_bridge PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-fpermissive>)
endif()
target_compile_definitions(sp_game_bridge PRIVATE NDEBUG EF_DYLIB_MODULES
  $<$<AND:$<BOOL:${APPLE}>,$<BOOL:${EF_COOP}>>:EF_COOP>)   # co-op's engine side is macOS only
if(WIN32)
  target_compile_definitions(sp_game_bridge PRIVATE NOMINMAX)
endif()
set_property(TARGET sp_game_bridge PROPERTY CXX_STANDARD 17)

# ---- efsp ----
add_executable(efsp
  ${CLIENT_SRC} ${QCOMMON_SRC} ${SERVER_SRC} ${BOTLIB_SRC}
  ${SDL_SRC} ${SYS_SRC}
  ${RENDERER_SRC} ${RENDERCOMMON_SRC}
  ${ZLIB_SRC} ${JPEG_SRC} ${LIBMAD_SRC}
  ${SP}/sp_integration.c ${BINK_SRC})
set_target_properties(efsp PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EF_ENGINE_OUTPUT_DIR}")
if(WIN32 AND EF_WIN_GUI)
  set_target_properties(efsp PROPERTIES WIN32_EXECUTABLE ON)
endif()

target_include_directories(efsp PRIVATE
  ${EF_COMPAT_DIR}               # android/log.h shim
  ${EF} ${EF}/zlib ${EF}/jpeg-8c ${EF}/AL
  ${EF}/libmad-0.15.1b ${EF}/libmad-0.15.1b/include
  ${SP})

# libmad fixed-point math: 64-bit multiply on arm64/x86_64, x86 asm on 32-bit x86
if(EF_ARCH STREQUAL "x86")
  set(EF_LIBMAD_FPM FPM_INTEL)
elseif(EF_ARCH STREQUAL "armv7")
  set(EF_LIBMAD_FPM FPM_ARM)
else()
  set(EF_LIBMAD_FPM FPM_64BIT)
endif()

if(APPLE)
  set(EF_HOMEPATH_NAME "EliteForce-MacOS")
  set(EF_PLATFORM_DEFS
    USE_CURL USE_SYSTEM_CURL	# HTTP map downloads (sv_dlURL) via the macOS system libcurl
    MACOS_X
    MACOS_HOMEPATH_NAME="${EF_HOMEPATH_NAME}"
    $<$<BOOL:${EF_COOP}>:EF_COOP>)   # until the sys layer uses EF_HOMEPATH_NAME
else()
  set(EF_HOMEPATH_NAME "EliteForce-Marino")
  # libcurl loaded at runtime if present (libcurl-4.dll / libcurl.so.4); headers from
  # efcode/curl-7.54.0/include (USE_LOCAL_HEADERS without USE_SYSTEM_CURL)
  set(EF_PLATFORM_DEFS USE_CURL USE_CURL_DLOPEN)
endif()

if(NOT APPLE)
  # The Windows/Linux launcher core (efcode/launcher). efsp compiles the zlib sources
  # itself, so the core doesn't bundle its own; GOG unpacking follows EF_GOG.
  set(LC_BUNDLE_ZLIB OFF CACHE BOOL "" FORCE)
  set(LC_GOG ${EF_GOG} CACHE BOOL "" FORCE)
  add_subdirectory(${EF}/launcher eflauncher)
endif()

target_compile_definitions(efsp PRIVATE
  ELITEFORCE BOTLIB NDEBUG
  USE_CODEC_MP3 USE_INTERNAL_MP3 ${EF_LIBMAD_FPM}
  $<$<NOT:$<BOOL:${EF_VM_JIT}>>:NO_VM_COMPILED>
  USE_VULKAN USE_VULKAN_API
  USE_FLEXIBLE_DISPLAY=1
  USE_OPENAL USE_OPENAL_DLOPEN
  ${EF_PLATFORM_DEFS}
  PRODUCT_VERSION="1.40"
  EF_APP_VERSION="${EF_APP_VERSION}"
  ARCH_STRING="${EF_ARCH}"
  USE_LOCAL_HEADERS=1
  EF_HOMEPATH_NAME="${EF_HOMEPATH_NAME}")
if(WIN32)
  # no WIN32_LEAN_AND_MEAN: sys_win32.c needs <mmsystem.h> (timeGetTime, timeBeginPeriod)
  target_compile_definitions(efsp PRIVATE NOMINMAX UNICODE _UNICODE)
endif()

target_compile_options(efsp PRIVATE ${EF_ENGINE_COPTS})

target_link_libraries(efsp PRIVATE sp_game_bridge ${EF_SDL_TARGET})
if(APPLE)
  target_link_libraries(efsp PRIVATE
    "-framework Cocoa" "-framework IOKit" "-framework WebKit" dl m)
  if(EF_GOG)
    target_link_libraries(efsp PRIVATE lzma)	# the system's liblzma, for GOG installers (sys_osx_gog.c)
  endif()
  target_link_libraries(efsp PRIVATE curl)	# the system's libcurl (/usr/lib/libcurl.4.dylib), for HTTP map downloads
  if(EF_COOP)
    # the system's Compression library (libcompression), for co-op's replica frames (sys_coop_net.c)
    target_link_libraries(efsp PRIVATE compression)
  endif()
elseif(WIN32)
  target_link_libraries(efsp PRIVATE ws2_32 winmm iphlpapi dbghelp psapi advapi32 shell32 ole32)
  # libgcc / libstdc++ / winpthread linked in: no mingw runtime DLLs to ship
  target_link_options(efsp PRIVATE -static)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(efsp PRIVATE ${CMAKE_DL_LIBS} Threads::Threads m)
  # find libefgame.so / libefui.so and bundled libs next to the binary or in ../lib
  set_target_properties(efsp PROPERTIES BUILD_RPATH "\$ORIGIN;\$ORIGIN/../lib"
    INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../lib")
endif()
if(NOT APPLE)
  target_link_libraries(efsp PRIVATE eflauncher)	# the launcher core (GOG's sys_osx_gog.c and liblzma come with it)
  if(WIN32 AND EF_WIN_LAUNCHER)
    target_compile_definitions(efsp PRIVATE EF_WIN_LAUNCHER)
  endif()
endif()
if(EF_BINK)
  target_link_libraries(efsp PRIVATE PkgConfig::FFMPEG)
endif()
