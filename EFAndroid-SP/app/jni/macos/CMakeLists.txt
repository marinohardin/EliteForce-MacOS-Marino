cmake_minimum_required(VERSION 3.16)
project(efsp_macos C CXX OBJC)

# ---------------------------------------------------------------------------
# Elite Force engine (single player + Holomatch) for macOS (arm64). Mirrors ../src/CMakeLists.txt (the
# Android libmain.so build) but produces an executable, uses Homebrew SDL2 and
# FFmpeg, the lilium-voyager macOS sys layer (sys_osx.m, con_tty.c), and loads
# Vulkan (MoltenVK) at runtime through SDL_Vulkan_LoadLibrary.
#
#   cmake -S EFAndroid-SP/app/jni/macos -B build-macos
#   cmake --build build-macos
#   -> build-macos/efsp  (put libefgame.dylib / libefui.dylib next to it)
# ---------------------------------------------------------------------------

# the app's version (make-app.sh passes its VERSION), shown as "Elite Force for Mac <version>"
set(EF_APP_VERSION "1.2.3" CACHE STRING "App version shown in the console and to server browsers")
option(EF_BINK "Bink cinematics via Homebrew FFmpeg (OFF = stub, no movies)" ON)
option(EF_VM_JIT "aarch64 QVM JIT (needs MAP_JIT work on macOS)" OFF)

set(JNI ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(EF  ${JNI}/efcode)
set(SP  ${JNI}/sp)
set(EFG ${JNI}/../../../efgame)

find_package(PkgConfig REQUIRED)
# SDL2 (sdl2-compat on SDL3) from macos/build-deps.sh, built for the app's minimum
# macOS, when present; else pkg-config's default (Homebrew, the build Mac's macOS)
set(_ef_deps_default "${CMAKE_CURRENT_SOURCE_DIR}/../../../../build-deps/install")
if(NOT EXISTS "${_ef_deps_default}/lib/pkgconfig/sdl2.pc")
  set(_ef_deps_default "")
endif()
set(EF_DEPS_PREFIX "${_ef_deps_default}" CACHE PATH "SDL2/SDL3/Vulkan loader prefix from macos/build-deps.sh (empty = pkg-config default)")
if(EF_DEPS_PREFIX)
  get_filename_component(EF_DEPS_PREFIX "${EF_DEPS_PREFIX}" ABSOLUTE)
  set(ENV{PKG_CONFIG_PATH} "${EF_DEPS_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
endif()
unset(__pkg_config_checked_SDL2 CACHE)
unset(pkgcfg_lib_SDL2_SDL2 CACHE)
unset(pkgcfg_lib_SDL2_SDL2main CACHE)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
message(STATUS "SDL2: ${SDL2_LINK_LIBRARIES}")
if(EF_BINK)
  # Prefer the minimal LGPL FFmpeg from macos/build-ffmpeg.sh over Homebrew's
  # (GPL-3.0) one; make-app.sh bundles whichever was linked.
  set(_ef_ffmpeg_default "${CMAKE_CURRENT_SOURCE_DIR}/../../../../build-ffmpeg/install")
  if(NOT EXISTS "${_ef_ffmpeg_default}/lib/pkgconfig/libavcodec.pc")
    set(_ef_ffmpeg_default "")
  endif()
  set(EF_FFMPEG_PREFIX "${_ef_ffmpeg_default}" CACHE PATH "FFmpeg install prefix (empty = pkg-config default, e.g. Homebrew)")
  if(EF_FFMPEG_PREFIX)
    get_filename_component(EF_FFMPEG_PREFIX "${EF_FFMPEG_PREFIX}" ABSOLUTE)
    set(ENV{PKG_CONFIG_PATH} "${EF_FFMPEG_PREFIX}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
  endif()
  # re-resolve on every configure, so a changed prefix takes effect
  unset(__pkg_config_checked_FFMPEG CACHE)
  foreach(_lib avformat avcodec swscale avutil)
    unset(pkgcfg_lib_FFMPEG_${_lib} CACHE)
  endforeach()
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libswscale libavutil)
  message(STATUS "FFmpeg: ${FFMPEG_LINK_LIBRARIES}")
endif()

set(CLIENT_SRC
  ${EF}/client/cl_cgame.c   ${EF}/client/cl_cin.c     ${EF}/client/cl_console.c
  ${EF}/client/cl_input.c   ${EF}/client/cl_keys.c    ${EF}/client/cl_main.c
  ${EF}/client/cl_net_chan.c ${EF}/client/cl_parse.c  ${EF}/client/cl_scrn.c
  ${EF}/client/cl_ui.c      ${EF}/client/cl_avi.c
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
  ${EF}/qcommon/vm.c        ${EF}/qcommon/vm_interpreted.c)
if(EF_VM_JIT)
  list(APPEND QCOMMON_SRC ${EF}/qcommon/vm_aarch64.c)
endif()

set(SERVER_SRC
  ${EF}/server/sv_bot.c     ${EF}/server/sv_ccmds.c   ${EF}/server/sv_client.c
  ${EF}/server/sv_game.c    ${EF}/server/sv_init.c    ${EF}/server/sv_main.c
  ${EF}/server/sv_net_chan.c ${EF}/server/sv_snapshot.c ${EF}/server/sv_world.c)

file(GLOB BOTLIB_SRC ${EF}/botlib/*.c)

set(SDL_SRC ${EF}/sdl/sdl_input.c ${EF}/sdl/sdl_snd.c ${EF}/sdl/sdl_gamma.c ${EF}/sdl/sdl_glimp.c)

# lilium-voyager's macOS sys layer: tty console + Cocoa dialogs (sys_osx.m).
set(SYS_SRC
  ${EF}/sys/con_log.c ${EF}/sys/con_tty.c ${EF}/sys/sys_autoupdater.c
  ${EF}/sys/sys_unix.c ${EF}/sys/sys_main.c ${EF}/sys/sys_osx.m
  ${EF}/sys/sys_osx_data.m ${EF}/sys/sys_osx_launcher.m ${EF}/sys/sys_osx_server.m
  ${EF}/sys/sys_osx_gog.c)
set_source_files_properties(${EF}/sys/sys_osx_launcher.m ${EF}/sys/sys_osx_server.m
  PROPERTIES COMPILE_OPTIONS -fobjc-arc)

file(GLOB RENDERCOMMON_SRC ${EF}/renderercommon/*.c)
file(GLOB RENDERER_SRC ${EF}/renderervk/*.c)
list(REMOVE_ITEM RENDERER_SRC ${EF}/renderervk/tr_subs.c)
list(REMOVE_ITEM RENDERER_SRC ${EF}/renderervk/shaders/spirv/shader_data.c)

file(GLOB ZLIB_SRC ${EF}/zlib/*.c)
file(GLOB JPEG_SRC ${EF}/jpeg-8c/*.c)

if(EF_BINK)
  set(BINK_SRC ${SP}/bink_ff.c)
else()
  set(BINK_SRC ${CMAKE_CURRENT_SOURCE_DIR}/bink_stub.c)
endif()

# ---- SP game/cgame bridge (C++ against the SP game headers) ----
add_library(sp_game_bridge STATIC
  ${SP}/sp_bridge.cpp ${SP}/sp_ui_bridge.cpp ${SP}/sp_clink.c)
target_include_directories(sp_game_bridge PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}               # android/log.h shim
  ${EFG}/android ${EFG}/src/game ${EFG}/src/cgame ${EFG}/src/ui
  ${EFG}/src/client ${EFG}/src/renderer ${EFG}/src/qcommon ${SP})
target_compile_options(sp_game_bridge PRIVATE
  -include ${EFG}/android/ef_android_compat.h -fno-threadsafe-statics -fcommon -w)
target_compile_definitions(sp_game_bridge PRIVATE NDEBUG EF_DYLIB_MODULES)
set_property(TARGET sp_game_bridge PROPERTY CXX_STANDARD 17)

add_executable(efsp
  ${CLIENT_SRC} ${QCOMMON_SRC} ${SERVER_SRC} ${BOTLIB_SRC}
  ${SDL_SRC} ${SYS_SRC}
  ${RENDERER_SRC} ${RENDERCOMMON_SRC}
  ${ZLIB_SRC} ${JPEG_SRC} ${LIBMAD_SRC}
  ${SP}/sp_integration.c ${BINK_SRC})

target_include_directories(efsp PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}               # android/log.h shim
  ${EF} ${EF}/zlib ${EF}/jpeg-8c ${EF}/AL
  ${EF}/libmad-0.15.1b ${EF}/libmad-0.15.1b/include
  ${SP})

target_compile_definitions(efsp PRIVATE
  ELITEFORCE BOTLIB NDEBUG
  USE_CODEC_MP3 USE_INTERNAL_MP3 FPM_64BIT
  $<$<NOT:$<BOOL:${EF_VM_JIT}>>:NO_VM_COMPILED>
  USE_VULKAN USE_VULKAN_API
  USE_FLEXIBLE_DISPLAY=1
  USE_OPENAL USE_OPENAL_DLOPEN
  PRODUCT_VERSION="1.40"
  EF_APP_VERSION="${EF_APP_VERSION}"
  ARCH_STRING="arm64"
  USE_LOCAL_HEADERS=1
  MACOS_X
  MACOS_HOMEPATH_NAME="EliteForce-MacOS")

target_compile_options(efsp PRIVATE
  -fno-strict-aliasing -Werror=implicit-function-declaration -O2 -w
  # Apple clang 17+ errors on these by default; NDK r25 clang only warned.
  -Wno-error=incompatible-function-pointer-types -Wno-error=int-conversion)

target_link_libraries(efsp PRIVATE sp_game_bridge PkgConfig::SDL2
  "-framework Cocoa" "-framework IOKit" "-framework WebKit" dl m
  lzma)		# the system's liblzma, for GOG installers (sys_osx_gog.c)
if(EF_BINK)
  target_link_libraries(efsp PRIVATE PkgConfig::FFMPEG)
endif()
