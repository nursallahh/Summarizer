# C++ core library: built as smz_core.dll on Windows.
add_library(smz_core SHARED
    src/smz_api.cpp
)

configure_file(src/smz_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/smz_version.h @ONLY)

target_include_directories(smz_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)

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
