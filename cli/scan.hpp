#pragma once

#include <string>
#include <vector>

namespace cli {

// `summarizer scan ...`; `args` are the arguments after "scan", UTF-8.
int run_scan(const std::vector<std::string>& args);

void print_scan_help();

}  // namespace cli
