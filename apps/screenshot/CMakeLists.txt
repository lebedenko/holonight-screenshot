add_library(screenshot-controller STATIC capture_controller.cpp)
target_link_libraries(screenshot-controller PUBLIC screenshot-capture)
target_include_directories(screenshot-controller PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(screenshot-grim STATIC grim_acquisition.cpp)
target_link_libraries(screenshot-grim PUBLIC screenshot-capture)
target_include_directories(screenshot-grim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(holonight-screenshot main.cpp)
target_compile_definitions(holonight-screenshot PRIVATE SCREENSHOT_VERSION="${PROJECT_VERSION}")
target_link_libraries(holonight-screenshot PRIVATE screenshot-controller screenshot-grim)
