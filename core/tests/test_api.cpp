// Tests for the core C interface.
// No test framework on purpose: step 1 keeps zero dependencies.
// We will switch to GoogleTest or Catch2 once the number of tests grows.

#include "smz/smz_api.h"

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
    const char* version = smz_version();
    check(version != nullptr && std::strlen(version) > 0, "smz_version returns a non-empty string");
    check(std::string(version) == SMZ_TEST_EXPECTED_VERSION, "smz_version matches the CMake project version");

    // The ABI version in the header must match the one compiled into the DLL.
    check(smz_abi_version() == SMZ_ABI_VERSION, "smz_abi_version matches SMZ_ABI_VERSION in the header");

    // Every status code must have a message.
    check(std::strlen(smz_status_message(SMZ_OK)) > 0, "SMZ_OK has a message");
    check(std::strlen(smz_status_message(SMZ_ERR_INTERNAL)) > 0, "SMZ_ERR_INTERNAL has a message");
    check(std::strlen(smz_status_message(static_cast<smz_status>(999))) > 0, "Unknown status code has a message");

    // A NULL output parameter must be rejected, not crash.
    check(smz_build_info(nullptr) == SMZ_ERR_INVALID_ARGUMENT, "smz_build_info(NULL) returns invalid argument");

    // Build info must return valid text that can be freed.
    char* json = nullptr;
    const smz_status status = smz_build_info(&json);
    check(status == SMZ_OK, "smz_build_info succeeds");
    check(json != nullptr && json[0] == '{', "smz_build_info returns a JSON object");
    check(json != nullptr && std::strstr(json, SMZ_TEST_EXPECTED_VERSION) != nullptr, "JSON contains the version");
    smz_free_string(json);

    // Freeing NULL must be safe.
    smz_free_string(nullptr);
    check(true, "smz_free_string(NULL) does not crash");

    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll tests passed\n");
    return 0;
}
