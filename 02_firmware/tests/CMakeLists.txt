add_executable(fc_tests test_main.c)
target_link_libraries(fc_tests PRIVATE fc_core fc_hal m)
add_test(NAME fc_unit_tests COMMAND fc_tests)
