# The launcher core (docs/launcher-spec/ARCHITECTURE.md): a static C library for the
# Windows and Linux launchers. Not part of the macOS app; it also builds on macOS
# so its unit tests (tests/launcher) run on the development Mac.
#
#   add_subdirectory(<this dir> eflauncher)   ->  target eflauncher
#
# Options:
#   LC_GOG          unpack GOG installers: compiles ../sys/sys_osx_gog.c (unchanged) and links
#                   the static liblzma of the deps prefix (macOS test builds: Homebrew's xz;
#                   without liblzma the core builds with LC_HAVE_GOG 0)        default ON
#   LC_BUNDLE_ZLIB  compile the engine's zlib sources (../zlib) into eflauncher, for
#                   sys_osx_gog.c's inflate/crc32. The engine turns it OFF: efsp has them.
#   LC_DEPS_PREFIX  where liblzma lives (default: build-deps/<platform>/install of the checkout)
#
# Each core branch appends its sources to LC_SOURCES.

set(LC_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/lc_common.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_settings.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_links.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_update.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_launch.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_data.c
  # core-b: servers, queries, hosting, router port mapping, co-op
  ${CMAKE_CURRENT_LIST_DIR}/lc_server.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_net.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_host.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_nat.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_coop.c
)

option(LC_GOG "Launcher core: unpack GOG installers (sys_osx_gog.c + liblzma)" ON)
option(LC_BUNDLE_ZLIB "Launcher core: compile the engine's zlib sources into eflauncher" ON)

set(_lc_root "${CMAKE_CURRENT_LIST_DIR}/../../../../..")
get_filename_component(_lc_root "${_lc_root}" ABSOLUTE)
if(APPLE)
  set(_lc_default_deps "/opt/homebrew/opt/xz")
elseif(WIN32)
  set(_lc_default_deps "${_lc_root}/build-deps/windows/install")
else()
  set(_lc_default_deps "${_lc_root}/build-deps/linux/install")
endif()
set(LC_DEPS_PREFIX "${_lc_default_deps}" CACHE PATH "Prefix with lib/liblzma.a for the launcher core's GOG support")

set(LC_HAVE_GOG 0)
if(LC_GOG)
  set(_lc_hints "${LC_DEPS_PREFIX}")
  if(DEFINED EF_DEPS_PREFIX AND EF_DEPS_PREFIX)
    list(APPEND _lc_hints "${EF_DEPS_PREFIX}")
  endif()
  find_library(LC_LZMA_LIBRARY NAMES liblzma.a lzma HINTS ${_lc_hints} PATH_SUFFIXES lib
    NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
  if(NOT LC_LZMA_LIBRARY AND NOT CMAKE_CROSSCOMPILING)
    find_library(LC_LZMA_LIBRARY NAMES lzma)
  endif()
  if(LC_LZMA_LIBRARY)
    set(LC_HAVE_GOG 1)
    list(APPEND LC_SOURCES ${CMAKE_CURRENT_LIST_DIR}/../sys/sys_osx_gog.c)
  else()
    message(STATUS "eflauncher: liblzma not found (LC_DEPS_PREFIX=${LC_DEPS_PREFIX}): building without GOG installer support")
  endif()
endif()

if(LC_BUNDLE_ZLIB AND LC_HAVE_GOG)
  file(GLOB LC_ZLIB_SOURCES ${CMAKE_CURRENT_LIST_DIR}/../zlib/*.c)
  list(APPEND LC_SOURCES ${LC_ZLIB_SOURCES})
  set_source_files_properties(${LC_ZLIB_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

add_library(eflauncher STATIC ${LC_SOURCES})
target_include_directories(eflauncher PUBLIC ${CMAKE_CURRENT_LIST_DIR})
set_target_properties(eflauncher PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
target_compile_definitions(eflauncher PUBLIC LC_HAVE_GOG=${LC_HAVE_GOG})
if(LC_HAVE_GOG)
  target_link_libraries(eflauncher PUBLIC ${LC_LZMA_LIBRARY})
if(DEFINED EF_APP_VERSION)
  target_compile_definitions(eflauncher PRIVATE EF_APP_VERSION="${EF_APP_VERSION}")
endif()
if(DEFINED EF_HOMEPATH_NAME)
  target_compile_definitions(eflauncher PUBLIC EF_HOMEPATH_NAME="${EF_HOMEPATH_NAME}")
endif()
if(WIN32)
  target_compile_definitions(eflauncher PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0601)
  target_link_libraries(eflauncher PUBLIC ws2_32 iphlpapi shell32 ole32 advapi32 secur32)
elseif(APPLE)
else()
  find_package(Threads REQUIRED)
  target_link_libraries(eflauncher PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()
