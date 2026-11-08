# Minimal, self-contained CMake toolchain for the Windows dependency builds
# (windows/build-deps.sh): GCC mingw-w64 x86_64, from Homebrew or Ubuntu.
# libgcc is linked statically so no libgcc_s_seh-1.dll / libwinpthread-1.dll
# is needed at run time.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(_triple x86_64-w64-mingw32)
set(CMAKE_C_COMPILER ${_triple}-gcc)
set(CMAKE_CXX_COMPILER ${_triple}-g++)
set(CMAKE_RC_COMPILER ${_triple}-windres)
set(CMAKE_AR ${_triple}-ar CACHE FILEPATH "")
set(CMAKE_RANLIB ${_triple}-ranlib CACHE FILEPATH "")
set(CMAKE_DLLTOOL ${_triple}-dlltool CACHE FILEPATH "")
set(CMAKE_C_FLAGS_INIT "-static-libgcc")
set(CMAKE_CXX_FLAGS_INIT "-static-libgcc -static-libstdc++")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libgcc")
# look for libraries/headers only in the cross prefix, never the host's
if(DEFINED ENV{EF_WIN_PREFIX})
	list(APPEND CMAKE_FIND_ROOT_PATH "$ENV{EF_WIN_PREFIX}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
