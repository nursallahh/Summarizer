# C++ çekirdek kütüphane: Windows'ta da_core.dll olarak derlenir.
add_library(da_core SHARED
    src/da_api.cpp
)

configure_file(src/da_version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/da_version.h @ONLY)

target_include_directories(da_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated
)

# DLL'i derlerken fonksiyonlar dışa aktarılır (dllexport), kullanırken içe alınır (dllimport).
target_compile_definitions(da_core PRIVATE DA_CORE_BUILDING)

# Yalnızca da_api.h'de DA_API ile işaretlenen fonksiyonlar dışarı açılsın.
set_target_properties(da_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
)

# Derlenen DLL'i Unity projesine kopyala (unity/ klasörü varsa).
set(DA_UNITY_PLUGIN_DIR ${PROJECT_SOURCE_DIR}/unity/Assets/Plugins/x86_64)
if(WIN32 AND DA_COPY_TO_UNITY AND EXISTS ${PROJECT_SOURCE_DIR}/unity/Assets)
    add_custom_command(TARGET da_core POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory ${DA_UNITY_PLUGIN_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:da_core> ${DA_UNITY_PLUGIN_DIR}
        COMMENT "da_core.dll Unity projesine kopyalanıyor"
    )
endif()
