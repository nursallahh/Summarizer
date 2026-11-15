cmake_minimum_required(VERSION 3.25)

project(Summarizer
    VERSION 0.0.1
    DESCRIPTION "Summarizer - C++ core and command-line tool"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# On Windows, put the DLL and executables in the same folder so the DLL is found at run time.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(SMZ_BUILD_TESTS "Build the core tests" ON)
option(SMZ_COPY_TO_UNITY "Copy the built DLL into unity/Assets/Plugins/x86_64" ON)

if(MSVC)
    # UTF-8 source files, strict warnings
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(cli)

if(SMZ_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()
