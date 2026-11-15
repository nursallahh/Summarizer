#include "da/da_api.h"

#include "da_version.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <new>
#include <string>

namespace {

// Copies a std::string into a malloc-allocated C string that can cross the C boundary.
// It is released with da_free_string (free); new/delete are never mixed with it.
char* copy_to_c_string(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

std::string compiler_name() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown";
#endif
}

}  // namespace

extern "C" {

const char* da_version(void) {
    return DA_VERSION_STRING;
}

int32_t da_abi_version(void) {
    return DA_ABI_VERSION;
}

const char* da_status_message(da_status status) {
    switch (status) {
        case DA_OK:
            return "Success";
        case DA_ERR_INVALID_ARGUMENT:
            return "Invalid argument";
        case DA_ERR_OUT_OF_MEMORY:
            return "Out of memory";
        case DA_ERR_INTERNAL:
            return "Unexpected internal error";
    }
    return "Unknown status code";
}

da_status da_build_info(char** out_json) {
    if (out_json == nullptr) {
        return DA_ERR_INVALID_ARGUMENT;
    }
    *out_json = nullptr;

    // Exceptions must never cross the C boundary: Unity cannot catch them and would crash.
    try {
        std::string json;
        json += "{\"version\":\"";
        json += DA_VERSION_STRING;
        json += "\",\"abi\":";
        json += std::to_string(DA_ABI_VERSION);
        json += ",\"compiler\":\"";
        json += compiler_name();
        json += "\",\"build_type\":\"";
        json += DA_BUILD_TYPE;
        json += "\"}";

        *out_json = copy_to_c_string(json);
        return *out_json != nullptr ? DA_OK : DA_ERR_OUT_OF_MEMORY;
    } catch (const std::bad_alloc&) {
        return DA_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return DA_ERR_INTERNAL;
    }
}

void da_free_string(char* str) {
    std::free(str);
}

}  // extern "C"
