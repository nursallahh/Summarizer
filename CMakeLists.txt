cmake_minimum_required(VERSION 3.25)

project(DersArsivi
    VERSION 0.0.1
    DESCRIPTION "Ders Arsivi - C++ cekirdek ve komut satiri araci"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows'ta DLL ve exe ayni klasore dussun (calistirirken DLL bulunsun).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

option(DA_BUILD_TESTS "Cekirdek testlerini derle" ON)
option(DA_COPY_TO_UNITY "Derlenen DLL'i unity/Assets/Plugins/x86_64 altina kopyala" ON)

if(MSVC)
    # UTF-8 kaynak dosyalari, sik uyarilar
    add_compile_options(/utf-8 /W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_subdirectory(core)
add_subdirectory(cli)

if(DA_BUILD_TESTS)
    enable_testing()
    add_subdirectory(core/tests)
endif()
