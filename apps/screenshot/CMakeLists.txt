add_library(screenshot-controller STATIC capture_controller.cpp)
target_link_libraries(screenshot-controller PUBLIC screenshot-capture)
target_include_directories(screenshot-controller PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(screenshot-grim STATIC grim_acquisition.cpp)
target_link_libraries(screenshot-grim PUBLIC screenshot-capture)
target_include_directories(screenshot-grim PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(screenshot-selection STATIC region_selection.cpp)
target_link_libraries(screenshot-selection PUBLIC screenshot-capture)
target_include_directories(screenshot-selection PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_library(screenshot-output STATIC png_output.cpp)
target_link_libraries(screenshot-output PUBLIC Qt6::Widgets)
target_include_directories(screenshot-output PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(holonight-screenshot main.cpp region_overlay.cpp)
qt_add_resources(holonight-screenshot region-preview PREFIX "/screenshot" FILES RegionPreview.qml)
target_compile_definitions(holonight-screenshot PRIVATE SCREENSHOT_VERSION="${PROJECT_VERSION}")
target_link_libraries(holonight-screenshot PRIVATE screenshot-controller screenshot-grim
  screenshot-selection screenshot-output Qt6::QuickControls2 HolonightQt::Wayland)
