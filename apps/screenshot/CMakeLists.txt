add_library(screenshot-output STATIC png_output.cpp)
target_link_libraries(screenshot-output PUBLIC Qt6::Widgets PRIVATE HolonightScreenshot::Capture)
target_include_directories(screenshot-output PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(holonight-screenshot main.cpp)
target_compile_definitions(holonight-screenshot PRIVATE SCREENSHOT_VERSION="${PROJECT_VERSION}")
target_link_libraries(holonight-screenshot PRIVATE HolonightScreenshot::Capture screenshot-output)
install(TARGETS holonight-screenshot RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
