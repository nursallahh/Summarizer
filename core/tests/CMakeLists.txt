add_executable(smz_core_tests test_api.cpp)
target_link_libraries(smz_core_tests PRIVATE smz_core)
target_compile_definitions(smz_core_tests PRIVATE SMZ_TEST_EXPECTED_VERSION="${PROJECT_VERSION}")
add_test(NAME smz_core_api COMMAND smz_core_tests)

# Job and event tests. They also test header-only internals (event queue, JSON writer),
# so they link the internal library as well.
add_executable(smz_core_job_tests test_jobs.cpp)
target_link_libraries(smz_core_job_tests PRIVATE smz_core smz_core_internal)
add_test(NAME smz_core_jobs COMMAND smz_core_job_tests)
set_tests_properties(smz_core_jobs PROPERTIES TIMEOUT 60)

# File inspection and import tests. Test files are generated at run time.
add_executable(smz_core_file_tests test_files.cpp)
target_link_libraries(smz_core_file_tests PRIVATE smz_core smz_core_internal)
add_test(NAME smz_core_files COMMAND smz_core_file_tests)
set_tests_properties(smz_core_files PROPERTIES TIMEOUT 60)

# Office text, DEFLATE and the Gemini classifier (fake transport, no network).
add_executable(smz_core_classify_tests test_classify.cpp)
target_link_libraries(smz_core_classify_tests PRIVATE smz_core_internal)
target_compile_definitions(smz_core_classify_tests PRIVATE SMZ_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_test(NAME smz_core_classify COMMAND smz_core_classify_tests)
set_tests_properties(smz_core_classify PROPERTIES TIMEOUT 60)
