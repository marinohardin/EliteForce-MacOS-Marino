# Unit tests for the launcher core (plain C, no data, no network).
#   cmake -S tests/launcher -B build/launcher-tests && cmake --build build/launcher-tests
#   ctest --test-dir build/launcher-tests --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(eflauncher_tests C)
enable_testing()
add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../EFAndroid-SP/app/jni/efcode/launcher eflauncher)

# one executable per test_*.c
file(GLOB LC_TESTS ${CMAKE_CURRENT_LIST_DIR}/test_*.c)
foreach(t ${LC_TESTS})
  get_filename_component(name ${t} NAME_WE)
  add_executable(${name} ${t})
  target_link_libraries(${name} PRIVATE eflauncher)
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "LC_HOMEPATH=${CMAKE_CURRENT_BINARY_DIR}/home-${name}")
endforeach()
