#pragma once

// Tiny test helpers. No framework on purpose while the number of tests is small.

#include <cstdio>
#include <string>

namespace test {

inline int g_failures = 0;

inline void check(bool condition, const char* description) {
    if (condition) {
        std::printf("[PASS] %s\n", description);
    } else {
        std::printf("[FAIL] %s\n", description);
        ++g_failures;
    }
}

inline int count_occurrences(const std::string& text, const std::string& needle) {
    int count = 0;
    for (std::size_t pos = text.find(needle); pos != std::string::npos; pos = text.find(needle, pos + needle.size())) {
        ++count;
    }
    return count;
}

inline int finish() {
    if (g_failures > 0) {
        std::printf("\n%d test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("\nAll tests passed\n");
    return 0;
}

}  // namespace test
