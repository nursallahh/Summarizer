// summarizer: command-line tool that uses the core without Unity.
// In phase 0, accuracy tests and batch jobs run through this tool.

#include "smz/smz_api.h"

#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

namespace {

void print_help() {
    std::printf(
        "Summarizer command-line tool\n"
        "\n"
        "Usage:\n"
        "  summarizer --version   Show the core version\n"
        "  summarizer --info      Show build information as JSON\n"
        "  summarizer --help      Show this help\n");
}

int print_info() {
    char* json = nullptr;
    const smz_status status = smz_build_info(&json);
    if (status != SMZ_OK) {
        std::fprintf(stderr, "Error: %s\n", smz_status_message(status));
        return 1;
    }
    std::printf("%s\n", json);
    smz_free_string(json);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
#if defined(_WIN32)
    // Make UTF-8 output (e.g. Turkish file names) display correctly in the Windows console.
    SetConsoleOutputCP(CP_UTF8);
#endif

    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        print_help();
        return 0;
    }
    if (std::strcmp(argv[1], "--version") == 0 || std::strcmp(argv[1], "-v") == 0) {
        std::printf("summarizer %s\n", smz_version());
        return 0;
    }
    if (std::strcmp(argv[1], "--info") == 0) {
        return print_info();
    }

    std::fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
    print_help();
    return 2;
}
