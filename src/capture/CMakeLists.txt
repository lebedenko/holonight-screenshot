add_library(screenshot-capture STATIC capture_frame.cpp desktop_acquisition.h)
target_include_directories(screenshot-capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(screenshot-capture PUBLIC Qt6::Core Qt6::Gui)
