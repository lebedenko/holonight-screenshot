add_library(screenshot-capture STATIC
  capture_session.cpp capture_frame.cpp desktop_acquisition.h capture_controller.cpp grim_acquisition.cpp region_selection.cpp region_overlay.cpp png_encoder.cpp)
add_library(HolonightScreenshot::Capture ALIAS screenshot-capture)
set_target_properties(screenshot-capture PROPERTIES EXPORT_NAME Capture POSITION_INDEPENDENT_CODE ON)
target_compile_features(screenshot-capture PUBLIC cxx_std_23)
target_include_directories(screenshot-capture PUBLIC
  "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
  "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/holonight/screenshot>")
target_link_libraries(screenshot-capture PUBLIC Qt6::Core Qt6::Gui
  PRIVATE Qt6::Quick Qt6::QuickControls2 Wayland::Client LayerShellQt::Interface)
set_target_properties(screenshot-capture PROPERTIES AUTORCC ON)
target_sources(screenshot-capture PRIVATE region-preview.qrc)

install(TARGETS screenshot-capture EXPORT HolonightScreenshotTargets ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES capture_session.h capture_frame.h capture_controller.h desktop_acquisition.h grim_acquisition.h
  region_selection.h region_overlay.h png_encoder.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/holonight/screenshot)

set(toplevel_header "${CMAKE_CURRENT_BINARY_DIR}/wayland-ext-foreign-toplevel-list-v1-client-protocol.h")
set(toplevel_code "${CMAKE_CURRENT_BINARY_DIR}/wayland-ext-foreign-toplevel-list-v1-protocol.c")
add_custom_command(OUTPUT "${toplevel_header}" "${toplevel_code}"
  COMMAND Wayland::Scanner client-header "${toplevel_protocol}" "${toplevel_header}"
  COMMAND Wayland::Scanner private-code "${toplevel_protocol}" "${toplevel_code}"
  DEPENDS "${toplevel_protocol}" VERBATIM)
target_sources(screenshot-capture PRIVATE "${toplevel_header}" "${toplevel_code}")
target_include_directories(screenshot-capture PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
