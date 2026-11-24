add_executable(capture-test capture_test.cpp)
target_link_libraries(capture-test PRIVATE HolonightScreenshot::Capture Qt6::Test)
add_test(NAME capture COMMAND capture-test)
add_test(NAME cli COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:holonight-screenshot>
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake")
set_tests_properties(capture cli PROPERTIES TIMEOUT 10)

add_executable(grim-test grim_test.cpp)
target_link_libraries(grim-test PRIVATE HolonightScreenshot::Capture Qt6::Test)
add_test(NAME grim COMMAND grim-test)
set_tests_properties(grim PROPERTIES TIMEOUT 30)

add_executable(region-test region_test.cpp)
target_link_libraries(region-test PRIVATE HolonightScreenshot::Capture Qt6::Test)
add_test(NAME region COMMAND region-test)
set_tests_properties(region PROPERTIES TIMEOUT 10 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(png-output-test png_output_test.cpp)
target_link_libraries(png-output-test PRIVATE screenshot-output Qt6::Test)
add_test(NAME png-output COMMAND png-output-test)
set_tests_properties(png-output PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=")

if(NOT SCREENSHOT_USE_INSTALLED_CAPTURE)
  find_package(Wayland REQUIRED COMPONENTS Server)
  set(toplevel_server "${CMAKE_CURRENT_BINARY_DIR}/toplevel-server.h")
  add_custom_command(OUTPUT "${toplevel_server}"
    COMMAND Wayland::Scanner server-header "${toplevel_protocol}" "${toplevel_server}"
    DEPENDS "${toplevel_protocol}" VERBATIM)
  add_executable(session-test session_test.cpp "${toplevel_server}")
  target_include_directories(session-test PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(session-test PRIVATE HolonightScreenshot::Capture Qt6::Test Wayland::Server)
  target_compile_definitions(session-test PRIVATE SCREENSHOT_APP="$<TARGET_FILE:holonight-screenshot>")
  add_dependencies(session-test holonight-screenshot)
  add_test(NAME session COMMAND session-test)
  set_tests_properties(session PROPERTIES TIMEOUT 20)
endif()

add_executable(launcher-test launcher_test.cpp ../apps/screenshot/launcher.qrc)
set_target_properties(launcher-test PROPERTIES AUTORCC ON)
target_link_libraries(launcher-test PRIVATE Qt6::QuickControls2 Qt6::Test)
add_test(NAME launcher COMMAND launcher-test)
set_tests_properties(launcher PROPERTIES TIMEOUT 15
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=;QT_QUICK_CONTROLS_STYLE=Basic")
