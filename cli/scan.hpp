#pragma once

#include <string>
#include <vector>

namespace cli {

// `summarizer scan ...` (classify = false) and `summarizer classify ...` (classify = true).
// `args` are the arguments after the command name, UTF-8.
int run_scan(const std::vector<std::string>& args, bool classify);

void print_scan_help();

// `summarizer report <table.csv>`: accuracy of the suggestions against the reviewed courses.
int run_report(const std::vector<std::string>& args);

}  // namespace cli
