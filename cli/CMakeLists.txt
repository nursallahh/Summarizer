add_executable(summarizer_cli
    main.cpp
    scan.cpp
    report.cpp
    review_page.cpp
    csv.cpp
)
# Phase 0 tools also use core internals directly (classification, file reading).
target_link_libraries(summarizer_cli PRIVATE smz_core smz_core_internal)
set_target_properties(summarizer_cli PROPERTIES OUTPUT_NAME summarizer)

if(WIN32)
    # Runs the tool with the UTF-8 code page, so command-line arguments such as Turkish
    # folder names arrive as UTF-8 (Windows 10 1903 and later).
    target_sources(summarizer_cli PRIVATE summarizer.manifest)
endif()
