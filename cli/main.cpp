// summarizer: command-line tool that uses the core without Unity.
// In phase 0, accuracy tests and batch jobs run through this tool.

#include "scan.hpp"
#include "smz/smz_api.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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
        "  summarizer scan <folder>       Build the table of course files in a folder\n"
        "  summarizer classify <folder>   Same, plus a course suggestion for each file and a review page\n"
        "  summarizer report <table.csv>  Accuracy of the suggestions against the reviewed courses\n"
        "  summarizer --version           Show the core version\n"
        "  summarizer --info              Show build information as JSON\n"
        "  summarizer --help              Show this help\n"
        "\n"
        "Run \"summarizer scan --help\" for details on scan and classify.\n");
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
    if (std::strcmp(argv[1], "scan") == 0) {
        // argv is UTF-8 on Windows too: summarizer.manifest sets the process code page to UTF-8.
        return cli::run_scan(std::vector<std::string>(argv + 2, argv + argc), false);
    }
    if (std::strcmp(argv[1], "classify") == 0) {
        return cli::run_scan(std::vector<std::string>(argv + 2, argv + argc), true);
    }
    if (std::strcmp(argv[1], "report") == 0) {
        return cli::run_report(std::vector<std::string>(argv + 2, argv + argc));
    }

    std::fprintf(stderr, "Unknown command: %s\n\n", argv[1]);
    print_help();
    return 2;
}
