# The launcher core (docs/launcher-spec/ARCHITECTURE.md): a static C library for the
# Windows and Linux launchers. Not part of the macOS app; it also builds on macOS
# so its unit tests (tests/launcher) run on the development Mac.
#
#   add_subdirectory(<this dir> eflauncher)   ->  target eflauncher
#
# Each core branch appends its sources to LC_SOURCES.

set(LC_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/lc_common.c
  # core-b: servers, queries, hosting, router port mapping, co-op
  ${CMAKE_CURRENT_LIST_DIR}/lc_server.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_net.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_host.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_nat.c
  ${CMAKE_CURRENT_LIST_DIR}/lc_coop.c
)

add_library(eflauncher STATIC ${LC_SOURCES})
target_include_directories(eflauncher PUBLIC ${CMAKE_CURRENT_LIST_DIR})
set_target_properties(eflauncher PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)
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
