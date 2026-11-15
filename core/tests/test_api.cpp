// Tests for the core C interface.
// No test framework on purpose: step 1 keeps zero dependencies.
// We will switch to GoogleTest or Catch2 once the number of tests grows.

#include "da/da_api.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_failures = 0;

void check(bool condition, const char* description) {
    if (condition) {
        std::printf("[PASS] %s\n", description);
    } else {
        std::printf("[FAIL] %s\n", description);
        ++g_failures;
    }
}

}  // namespace

int main() {
    // The version must be non-empty and match the CMake project version.
    const char* version = da_version();
    check(version != nullptr && std::strlen(version) > 0, "da_version returns a non-empty string");
    check(std::string(version) == DA_TEST_EXPECTED_VERSION, "da_version matches the CMake project version");

    // The ABI version in the header must match the one compiled into the DLL.
    check(da_abi_version() == DA_ABI_VERSION, "da_abi_version matches DA_ABI_VERSION in the header");

    // Every status code must have a message.
    check(std::strlen(da_status_message(DA_OK)) > 0, "DA_OK has a message");
    check(std::strlen(da_status_message(DA_ERR_INTERNAL)) > 0, "DA_ERR_INTERNAL has a message");
    check(std::strlen(da_status_message(static_cast<da_status>(999))) > 0, "Unknown status code has a message");

    // A NULL output parameter must be rejected, not crash.
    check(da_build_info(nullptr) == DA_ERR_INVALID_ARGUMENT, "da_build_info(NULL) returns invalid argument");

    // Build info must return valid text that can be freed.
    char* json = nullptr;
    const da_status status = da_build_info(&json);
    check(status == DA_OK, "da_build_info succeeds");
    check(json != nullptr && json[0] == '{', "da_build_info returns a JSON object");
    check(json != nullptr && std::strstr(json, DA_TEST_EXPECTED_VERSION) != nullptr, "JSON contains the version");
    da_free_string(json);

    // Freeing NULL must be safe.
    da_free_string(nullptr);
    check(true, "da_free_string(NULL) does not crash");

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll tests passed\n");
    return 0;
}
