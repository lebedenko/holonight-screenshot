cmake_minimum_required(VERSION 3.25)
project(holonight-screenshot VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core DBus Gui Widgets Quick QuickControls2)
find_package(Wayland REQUIRED COMPONENTS Client)
qt_standard_project_setup(REQUIRES 6.11)

include(GNUInstallDirs)
option(SCREENSHOT_USE_INSTALLED_CAPTURE "Build the CLI against the installed capture package" OFF)
if(SCREENSHOT_USE_INSTALLED_CAPTURE)
  find_package(HolonightScreenshot CONFIG REQUIRED)
else()
  enable_language(C)
  find_package(LayerShellQt 6.6 CONFIG REQUIRED)
  find_package(WaylandScanner REQUIRED)
  find_package(PkgConfig REQUIRED)
  pkg_get_variable(wayland_protocols_dir wayland-protocols pkgdatadir)
  set(toplevel_protocol "${wayland_protocols_dir}/staging/ext-foreign-toplevel-list/ext-foreign-toplevel-list-v1.xml")
  if(NOT EXISTS "${toplevel_protocol}")
    message(FATAL_ERROR "ext-foreign-toplevel-list-v1 protocol XML not found: ${toplevel_protocol}")
  endif()
  add_subdirectory(src/capture)
  set(package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/HolonightScreenshot")
  configure_file(cmake/HolonightScreenshotConfig.cmake.in HolonightScreenshotConfig.cmake @ONLY)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/HolonightScreenshotConfig.cmake" DESTINATION "${package_dir}")
  install(EXPORT HolonightScreenshotTargets NAMESPACE HolonightScreenshot:: DESTINATION "${package_dir}")
endif()
add_subdirectory(apps/screenshot)
option(BUILD_TESTING "Build capture and command-line tests" OFF)
if(BUILD_TESTING)
  enable_testing()
  find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
  add_subdirectory(tests)
endif()

add_custom_target(qml-lint
  COMMAND Qt6::qmllint --ignore-settings --max-warnings 0
    -I "${CMAKE_CURRENT_SOURCE_DIR}/tooling/qml"
    --resource "${CMAKE_CURRENT_SOURCE_DIR}/apps/screenshot/launcher.qrc"
    --resource "${CMAKE_CURRENT_SOURCE_DIR}/src/capture/region-preview.qrc"
    "${CMAKE_CURRENT_SOURCE_DIR}/apps/screenshot/Launcher.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/apps/screenshot/Editor.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/capture/RegionPreview.qml"
  VERBATIM)
