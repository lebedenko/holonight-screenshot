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
