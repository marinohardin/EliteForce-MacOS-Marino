# Unit tests for the launcher core (plain C, no data, no network beyond loopback).
#   cmake -S tests/launcher -B build/launcher-tests && cmake --build build/launcher-tests
#   ctest --test-dir build/launcher-tests --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(eflauncher_tests C)
enable_testing()
set(EFCODE ${CMAKE_CURRENT_LIST_DIR}/../../EFAndroid-SP/app/jni/efcode)
add_subdirectory(${EFCODE}/launcher eflauncher)

# one executable per test_*.c
file(GLOB LC_TESTS ${CMAKE_CURRENT_LIST_DIR}/test_*.c)
foreach(t ${LC_TESTS})
  get_filename_component(name ${t} NAME_WE)
  add_executable(${name} ${t})
  target_link_libraries(${name} PRIVATE eflauncher)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_LIST_DIR})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "LC_HOMEPATH=${CMAKE_CURRENT_BINARY_DIR}/home-${name}")
endforeach()

# the co-op compat header's zlib stand-ins: the engine's inflate-only zlib plus the vendored deflate sources
set(ZL ${EFCODE}/zlib)
target_sources(test_coop_compat PRIVATE
  ${ZL}/adler32.c ${ZL}/crc32.c ${ZL}/inffast.c ${ZL}/inflate.c ${ZL}/inftrees.c ${ZL}/zutil.c
  ${EFCODE}/zlib-deflate/deflate.c ${EFCODE}/zlib-deflate/trees.c ${EFCODE}/zlib-deflate/compress.c)
target_include_directories(test_coop_compat PRIVATE ${EFCODE}/sys ${ZL})
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(test_coop_compat PRIVATE -w)
endif()

# a manual check, not a ctest test: the real dedicated server through lc_proc_spawn
add_executable(check_dedicated ${CMAKE_CURRENT_LIST_DIR}/check_dedicated.c)
target_link_libraries(check_dedicated PRIVATE eflauncher)
target_include_directories(check_dedicated PRIVATE ${CMAKE_CURRENT_LIST_DIR})
