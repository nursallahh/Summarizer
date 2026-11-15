find_package(Threads REQUIRED)

# Internal C++ code of the core, as a static library.
# It is linked into smz_core.dll and directly into tests that need to check internals.
# Nothing in here is visible outside the DLL; the only public surface is smz_api.h.
add_library(smz_core_internal STATIC
    src/job_runner.cpp
    src/files/file_inspector.cpp
    src/files/sha256.cpp
)
target_include_directories(smz_core_internal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(smz_core_internal PUBLIC Threads::Threads)
set_target_properties(smz_core_internal PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# C++ core library: built as smz_core.dll on Windows.
add_library(smz_core SHARED
    src/smz_api.cpp
)

configure_file(src/smz_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/smz_version.h @ONLY)

target_include_directories(smz_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)
target_link_libraries(smz_core PRIVATE smz_core_internal)

# Functions are exported (dllexport) while building the DLL and imported (dllimport) by users.
target_compile_definitions(smz_core PRIVATE SMZ_CORE_BUILDING)

# Only functions marked with SMZ_API in smz_api.h are exported.
set_target_properties(smz_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Copy the built DLL into the Unity project (if the unity/ folder exists).
set(SMZ_UNITY_PLUGIN_DIR ${PROJECT_SOURCE_DIR}/unity/Assets/Plugins/x86_64)
if(WIN32 AND SMZ_COPY_TO_UNITY AND EXISTS ${PROJECT_SOURCE_DIR}/unity/Assets)
    add_custom_command(TARGET smz_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${SMZ_UNITY_PLUGIN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:smz_core> ${SMZ_UNITY_PLUGIN_DIR}
        COMMENT "Copying smz_core.dll into the Unity project"
    )
endif()
