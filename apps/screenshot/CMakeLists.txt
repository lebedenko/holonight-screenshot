find_package(tomlplusplus CONFIG REQUIRED)

add_library(screenshot-config STATIC config.cpp)
target_link_libraries(screenshot-config PUBLIC Qt6::Core PRIVATE tomlplusplus::tomlplusplus)
target_include_directories(screenshot-config PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(screenshot-output STATIC png_output.cpp)
target_link_libraries(screenshot-output PUBLIC Qt6::Widgets PRIVATE HolonightScreenshot::Capture)
target_include_directories(screenshot-output PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(screenshot-editor STATIC editor_document.cpp)
target_link_libraries(screenshot-editor PUBLIC Qt6::Gui)
target_include_directories(screenshot-editor PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(holonight-screenshot main.cpp workflow.cpp launcher.qrc)
set_target_properties(holonight-screenshot PROPERTIES AUTORCC ON)
target_compile_definitions(holonight-screenshot PRIVATE SCREENSHOT_VERSION="${PROJECT_VERSION}")
target_link_libraries(holonight-screenshot PRIVATE
  HolonightScreenshot::Capture screenshot-output screenshot-editor screenshot-config Qt6::DBus Qt6::Quick Wayland::Client)
install(TARGETS holonight-screenshot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES org.holonight.Screenshot.desktop DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES holonight-screenshot.svg DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
