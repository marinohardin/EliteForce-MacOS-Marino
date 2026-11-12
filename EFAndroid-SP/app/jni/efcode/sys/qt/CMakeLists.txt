# ef-launcher: the Qt 6 Widgets launcher for the Linux AppImage / Steam Deck port.
# Added by the engine CMake (Linux only, option EF_QT_LAUNCHER). Qt comes from
# build-deps/linux/qt (linux/build-qt.sh): qtbase + qtwayland, shared, no OpenGL.
#
# The executable sits next to efsp (EF_ENGINE_OUTPUT_DIR): Play execs the sibling efsp.

cmake_minimum_required(VERSION 3.16)

if(NOT Qt6_DIR AND DEFINED EF_DEPS_PREFIX AND EXISTS "${EF_DEPS_PREFIX}/../qt/lib/cmake/Qt6Widgets")
  list(APPEND CMAKE_PREFIX_PATH "${EF_DEPS_PREFIX}/../qt")
endif()
find_package(Qt6 6.4 QUIET COMPONENTS Core Gui Widgets Network)
if(NOT Qt6_FOUND)
  message(STATUS "ef-launcher: Qt 6 not found (set CMAKE_PREFIX_PATH to build-deps/linux/qt): not building the launcher")
  return()
endif()
message(STATUS "ef-launcher: Qt ${Qt6_VERSION} at ${Qt6_DIR}")

set(EFROOT ${CMAKE_CURRENT_LIST_DIR}/../..)		# efcode
get_filename_component(EF_REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)

# the portable launcher core. If the engine added it already (LC_BUNDLE_ZLIB off: efsp has zlib),
# ef-launcher, which has no engine, brings the engine's zlib sources itself.
if(NOT TARGET eflauncher)
  add_subdirectory(${EFROOT}/launcher eflauncher)
  set(EF_QT_NEED_ZLIB OFF)
else()
  set(EF_QT_NEED_ZLIB ON)
endif()
if(EF_QT_NEED_ZLIB)
  file(GLOB EF_QT_ZLIB_SRC ${EFROOT}/zlib/*.c)
  add_library(ef_qt_zlib STATIC ${EF_QT_ZLIB_SRC})
  target_include_directories(ef_qt_zlib PUBLIC ${EFROOT}/zlib)
  set_target_properties(ef_qt_zlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
  target_compile_options(ef_qt_zlib PRIVATE -w)
endif()

# JPEG decoding for the map thumbnails: the engine's bundled libjpeg (Qt is built with no
# image-format plugins)
file(GLOB EF_QT_JPEG_SRC ${EFROOT}/jpeg-8c/*.c)
add_library(ef_qt_jpeg STATIC ${EF_QT_JPEG_SRC})
target_include_directories(ef_qt_jpeg PUBLIC ${EFROOT}/jpeg-8c)
set_target_properties(ef_qt_jpeg PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(ef_qt_jpeg PRIVATE -w)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# the launcher's icon (header, About, server window) as a resource
set(EF_QT_ICON ${EF_REPO_ROOT}/linux/eliteforce-marino.png)
set(EF_QT_QRC ${CMAKE_CURRENT_BINARY_DIR}/ef_launcher_icon.qrc)
file(WRITE ${EF_QT_QRC} "<RCC><qresource prefix=\"/\"><file alias=\"icon.png\">${EF_QT_ICON}</file></qresource></RCC>\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${EF_QT_ICON})

add_executable(ef-launcher
  main.cpp ui_common.cpp widgets.cpp launcher_window.cpp hud_dialog.cpp data_ui.cpp coop_dialog.cpp
  server_config.cpp master_dialog.cpp browser_dialog.cpp server_window.cpp instance.cpp levelshot.cpp
  dialogs.h widgets.h launcher_window.h
  ${EF_QT_QRC})
set_target_properties(ef-launcher PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${EF_ENGINE_OUTPUT_DIR}")
target_include_directories(ef-launcher PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_compile_definitions(ef-launcher PRIVATE
  EF_APP_VERSION="${EF_APP_VERSION}" QT_NO_FOREACH QT_USE_QSTRINGBUILDER_OFF)
if(DEFINED EF_HOMEPATH_NAME)
  target_compile_definitions(ef-launcher PRIVATE EF_HOMEPATH_NAME="${EF_HOMEPATH_NAME}")
endif()
target_link_libraries(ef-launcher PRIVATE Qt6::Widgets Qt6::Network Qt6::Gui Qt6::Core eflauncher ef_qt_jpeg)
if(EF_QT_NEED_ZLIB)
  target_link_libraries(ef-launcher PRIVATE ef_qt_zlib)
endif()
# libstdc++ / libgcc linked in, like the rest of the AppImage's own binaries
target_link_options(ef-launcher PRIVATE -static-libstdc++ -static-libgcc)
# bundled libraries (Qt, ...) are found next to the binary or in ../lib
set_target_properties(ef-launcher PROPERTIES BUILD_RPATH "\$ORIGIN;\$ORIGIN/../lib"
  INSTALL_RPATH "\$ORIGIN;\$ORIGIN/../lib")
