# EFPlatform.cmake — platform / architecture / compiler detection shared by the
# engine (EFAndroid-SP/app/jni/engine) and the game modules (efgame).
#
# Include after project(). Sets (include_guard'ed):
#   EF_ROOT              repository root
#   EF_PLATFORM          macos | windows | linux | android
#   EF_ARCH              arm64 | x86_64 | armv7 | x86   (the engine's ARCH_STRING)
#   EF_COMPILER_CLANG    TRUE for Clang / AppleClang
#   EF_COMPILER_GCC      TRUE for GCC (e.g. Homebrew / Ubuntu mingw-w64, Linux gcc)
#   EF_NOWARN_FLAGS      silence every warning   (clang: -Wno-everything, GCC: -w)
#   EF_ERROR_LIMIT_FLAGS report every error      (clang: -ferror-limit=0, GCC: -fmax-errors=0)
#
# Functions:
#   ef_default_prefix(<out> <subdir> <probe>)   default dependency prefix for this platform
#   ef_use_deps_prefix(<var> <default> <doc>)   cache PATH var, put on pkg-config's search path

include_guard(GLOBAL)

get_filename_component(EF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(ANDROID)
  set(EF_PLATFORM android)
elseif(APPLE)
  set(EF_PLATFORM macos)
elseif(WIN32)
  set(EF_PLATFORM windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(EF_PLATFORM linux)
else()
  string(TOLOWER "${CMAKE_SYSTEM_NAME}" EF_PLATFORM)
endif()

# Target architecture. On macOS CMAKE_SYSTEM_PROCESSOR is the build host's CPU, so a single
# CMAKE_OSX_ARCHITECTURES entry wins (make-app.sh passes arm64 for the game modules).
set(_ef_proc "${CMAKE_SYSTEM_PROCESSOR}")
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(LENGTH CMAKE_OSX_ARCHITECTURES _ef_narch)
  if(_ef_narch EQUAL 1)
    set(_ef_proc "${CMAKE_OSX_ARCHITECTURES}")
  endif()
endif()
string(TOLOWER "${_ef_proc}" _ef_proc)
if(_ef_proc MATCHES "^(arm64|aarch64)$")
  set(EF_ARCH arm64)
elseif(_ef_proc MATCHES "^(x86_64|amd64|x64)$")
  set(EF_ARCH x86_64)
elseif(_ef_proc MATCHES "^arm")
  set(EF_ARCH armv7)
elseif(_ef_proc MATCHES "^(i.86|x86)$")
  set(EF_ARCH x86)
else()
  set(EF_ARCH "${_ef_proc}")
endif()

set(EF_COMPILER_CLANG FALSE)
set(EF_COMPILER_GCC FALSE)
set(_ef_cid "${CMAKE_CXX_COMPILER_ID}")
if(NOT _ef_cid)
  set(_ef_cid "${CMAKE_C_COMPILER_ID}")
endif()
if(_ef_cid MATCHES "Clang")
  set(EF_COMPILER_CLANG TRUE)
  set(EF_NOWARN_FLAGS -Wno-everything)
  set(EF_ERROR_LIMIT_FLAGS -ferror-limit=0)
elseif(_ef_cid STREQUAL "GNU")
  set(EF_COMPILER_GCC TRUE)
  set(EF_NOWARN_FLAGS -w)
  set(EF_ERROR_LIMIT_FLAGS -fmax-errors=0)
else()
  set(EF_NOWARN_FLAGS -w)
  set(EF_ERROR_LIMIT_FLAGS "")
endif()

# ef_default_prefix(OUT SUBDIR PROBE): the dependency prefix the per-platform scripts
# install into — macOS keeps macos/build-deps.sh's <root>/<subdir>/install, Windows and
# Linux use <root>/<subdir>/<platform>/install. Empty when <prefix>/<probe> does not exist
# (then pkg-config's default search path is used).
function(ef_default_prefix out subdir probe)
  if(EF_PLATFORM STREQUAL "macos")
    set(_p "${EF_ROOT}/${subdir}/install")
  else()
    set(_p "${EF_ROOT}/${subdir}/${EF_PLATFORM}/install")
  endif()
  if(NOT EXISTS "${_p}/${probe}")
    set(_p "")
  endif()
  set(${out} "${_p}" PARENT_SCOPE)
endfunction()

# ef_use_deps_prefix(VAR DEFAULT DOC): declare a dependency-prefix cache variable and, when
# set, put <prefix>/lib/pkgconfig first on pkg-config's search path. When cross-compiling,
# PKG_CONFIG_LIBDIR is limited to the given prefixes so the build host's (Homebrew) .pc
# files are never used for the target.
function(ef_use_deps_prefix var default doc)
  set(${var} "${default}" CACHE PATH "${doc}")
  if(${var})
    get_filename_component(_abs "${${var}}" ABSOLUTE)
    set(${var} "${_abs}" PARENT_SCOPE)
    set(ENV{PKG_CONFIG_PATH} "${_abs}/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
    if(CMAKE_CROSSCOMPILING)
      if("$ENV{PKG_CONFIG_LIBDIR}" STREQUAL "" OR "$ENV{PKG_CONFIG_LIBDIR}" STREQUAL "/nonexistent")
        set(ENV{PKG_CONFIG_LIBDIR} "${_abs}/lib/pkgconfig")
      else()
        set(ENV{PKG_CONFIG_LIBDIR} "${_abs}/lib/pkgconfig:$ENV{PKG_CONFIG_LIBDIR}")
      endif()
    endif()
  endif()
endfunction()

# When cross-compiling with no prefix at all, keep pkg-config away from the host's .pc files.
if(CMAKE_CROSSCOMPILING AND "$ENV{PKG_CONFIG_LIBDIR}" STREQUAL "")
  set(ENV{PKG_CONFIG_LIBDIR} "/nonexistent")
endif()
