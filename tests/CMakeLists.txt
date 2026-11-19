add_executable(capture-test capture_test.cpp)
target_link_libraries(capture-test PRIVATE screenshot-controller Qt6::Test)
add_test(NAME capture COMMAND capture-test)
add_test(NAME cli COMMAND ${CMAKE_COMMAND} -DAPP=$<TARGET_FILE:holonight-screenshot>
  -P "${CMAKE_CURRENT_SOURCE_DIR}/cli.cmake")
set_tests_properties(capture cli PROPERTIES TIMEOUT 10)

add_executable(grim-test grim_test.cpp)
target_link_libraries(grim-test PRIVATE screenshot-grim Qt6::Test)
add_test(NAME grim COMMAND grim-test)
set_tests_properties(grim PROPERTIES TIMEOUT 30)
