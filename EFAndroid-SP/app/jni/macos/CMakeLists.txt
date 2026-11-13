cmake_minimum_required(VERSION 3.16)
project(efsp_macos C CXX OBJC)

# ---------------------------------------------------------------------------
# macOS entry point kept for macos/make-app.sh: the engine build itself lives in
# ../engine/CMakeLists.txt (shared with Windows and Linux). Same options as before
# (EF_APP_VERSION, EF_BINK, EF_VM_JIT, EF_COOP, EF_DEPS_PREFIX, EF_FFMPEG_PREFIX).
#
#   cmake -S EFAndroid-SP/app/jni/macos -B build-macos
#   cmake --build build-macos
#   -> build-macos/efsp  (put libefgame.dylib / libefui.dylib next to it)
#
# This directory also holds the desktop android/log.h shim and bink_stub.c the engine uses.
# ---------------------------------------------------------------------------

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../engine ${CMAKE_CURRENT_BINARY_DIR}/engine)
