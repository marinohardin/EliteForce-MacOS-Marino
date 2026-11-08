cmake_minimum_required(VERSION 3.21)

# Elite Force for Mac/Windows/Linux — whole build: the engine (efsp) and the game modules
# (efgame / efui), all into the top of the build tree so efsp finds the modules next to it.
#
#   cmake --preset macos-arm64  && cmake --build --preset macos-arm64
#   cmake --preset windows-x64  && cmake --build --preset windows-x64   (mingw-w64 cross)
#   cmake --preset linux-x64    && cmake --build --preset linux-x64
#
# macos/make-app.sh still builds the two halves separately (cmake -S efgame and
# cmake -S EFAndroid-SP/app/jni/macos); both stay standalone projects.
# Options: EF_APP_VERSION, EF_BINK, EF_VM_JIT, EF_COOP, EF_GOG, EF_DEPS_PREFIX, EF_FFMPEG_PREFIX.

if(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  project(EliteForce C CXX OBJC)
else()
  project(EliteForce C CXX)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/EFPlatform.cmake)

option(EF_COOP "Single-player co-op (experimental)" OFF)
option(EF_BUILD_ENGINE "Build the engine (efsp)" ON)
option(EF_BUILD_GAME "Build the game modules (efgame, efui)" ON)

if(NOT CMAKE_RUNTIME_OUTPUT_DIRECTORY)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})   # efsp.exe, efgame.dll, efui.dll
endif()
if(NOT CMAKE_LIBRARY_OUTPUT_DIRECTORY)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})   # libefgame.so / .dylib
endif()

if(EF_BUILD_GAME)
  add_subdirectory(efgame)
endif()
if(EF_BUILD_ENGINE)
  add_subdirectory(EFAndroid-SP/app/jni/engine)
endif()
