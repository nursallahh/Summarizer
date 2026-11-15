add_executable(smz_core_tests test_api.cpp)
target_link_libraries(smz_core_tests PRIVATE smz_core)
target_compile_definitions(smz_core_tests PRIVATE SMZ_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")
add_test(NAME smz_core_api COMMAND smz_core_tests)

# Job and event tests. They also test header-only internals (event queue, JSON writer),
# so they see the core's private source folder.
add_executable(smz_core_job_tests test_jobs.cpp)
target_link_libraries(smz_core_job_tests PRIVATE smz_core)
target_include_directories(smz_core_job_tests PRIVATE ${PROJECT_SOURCE_DIR}/core/src)
add_test(NAME smz_core_jobs COMMAND smz_core_job_tests)
set_tests_properties(smz_core_jobs PROPERTIES TIMEOUT 60)
