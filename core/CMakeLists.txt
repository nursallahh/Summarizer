# C++ core library: built as da_core.dll on Windows.
add_library(da_core SHARED
    src/da_api.cpp
)

configure_file(src/da_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/da_version.h @ONLY)

target_include_directories(da_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# Functions are exported (dllexport) while building the DLL and imported (dllimport) by users.
target_compile_definitions(da_core PRIVATE DA_CORE_BUILDING)

# Only functions marked with DA_API in da_api.h are exported.
set_target_properties(da_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Copy the built DLL into the Unity project (if the unity/ folder exists).
set(DA_UNITY_PLUGIN_DIR ${PROJECT_SOURCE_DIR}/unity/Assets/Plugins/x86_64)
if(WIN32 AND DA_COPY_TO_UNITY AND EXISTS ${PROJECT_SOURCE_DIR}/unity/Assets)
    add_custom_command(TARGET da_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DA_UNITY_PLUGIN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:da_core> ${DA_UNITY_PLUGIN_DIR}
        COMMENT "Copying da_core.dll into the Unity project"
    )
endif()
