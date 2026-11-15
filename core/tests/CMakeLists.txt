add_executable(smz_core_tests test_api.cpp)
target_link_libraries(smz_core_tests PRIVATE smz_core)
target_compile_definitions(smz_core_tests PRIVATE SMZ_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")

add_test(NAME smz_core_api COMMAND smz_core_tests)
