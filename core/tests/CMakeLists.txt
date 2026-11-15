add_executable(da_core_tests test_api.cpp)
target_link_libraries(da_core_tests PRIVATE da_core)
target_compile_definitions(da_core_tests PRIVATE DA_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")

add_test(NAME da_core_api COMMAND da_core_tests)
