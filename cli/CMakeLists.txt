add_executable(summarizer_cli
    main.cpp
    scan.cpp
    csv.cpp
)
target_link_libraries(summarizer_cli PRIVATE smz_core)
set_target_properties(summarizer_cli PROPERTIES OUTPUT_NAME summarizer)

if(WIN32)
    # Runs the tool with the UTF-8 code page, so command-line arguments such as Turkish
    # folder names arrive as UTF-8 (Windows 10 1903 and later).
    target_sources(summarizer_cli PRIVATE summarizer.manifest)
endif()
