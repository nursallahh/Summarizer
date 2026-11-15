// summarizer report <table.csv>
//
// Compares the model's suggested_course with the course the person confirmed, and prints the
// numbers the design asks for: accuracy with its uncertainty, by file type and by the model's
// own confidence level, how much could be auto-accepted, and the most common mix-ups.
// Files without a confirmed course are reported as "not reviewed", never counted as correct.

#include "csv.hpp"
#include "scan.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace cli {

namespace {

struct Tally {
    int correct = 0;
    int total = 0;
};

// Wilson score interval, 95%.
std::pair<double, double> wilson(int k, int n) {
    if (n == 0) {
        return {0.0, 0.0};
    }
    const double z = 1.96;
    const double p = static_cast<double>(k) / n;
    const double denom = 1 + z * z / n;
    const double centre = (p + z * z / (2.0 * n)) / denom;
    const double half = z * std::sqrt(p * (1 - p) / n + z * z / (4.0 * n * n)) / denom;
    return {std::max(0.0, centre - half), std::min(1.0, centre + half)};
}

void print_line(const std::string& label, const Tally& t) {
    if (t.total == 0) {
        return;
    }
    const auto [lo, hi] = wilson(t.correct, t.total);
    std::printf("  %-26s %3d / %-3d  %5.1f%%   (95%% range %4.1f%% - %5.1f%%)\n", label.c_str(), t.correct, t.total,
                100.0 * t.correct / t.total, 100.0 * lo, 100.0 * hi);
}

}  // namespace

int run_report(const std::vector<std::string>& args) {
    if (args.size() != 1 || args[0] == "--help" || args[0] == "-h") {
        std::printf("summarizer report <table.csv>\n\n"
                    "  Accuracy of suggested_course against the reviewed course column.\n");
        return args.size() == 1 ? 0 : 2;
    }
    const std::string& arg = args[0];
    const fs::path file(std::u8string(reinterpret_cast<const char8_t*>(arg.data()), arg.size()));
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        std::fprintf(stderr, "Error: cannot open %s\n", arg.c_str());
        return 1;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::vector<CsvRow> rows = parse_csv(to_utf8_if_needed(buffer.str()));
    if (rows.empty()) {
        std::fprintf(stderr, "Error: the table is empty\n");
        return 1;
    }
    auto column = [&](const std::string& name) -> int {
        for (std::size_t i = 0; i < rows[0].size(); ++i) {
            if (rows[0][i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int course_col = column("course");
    const int sug_col = column("suggested_course");
    const int type_col = column("type");
    const int level_col = column("suggestion_level");
    const int name_col = column("file_name");
    if (course_col < 0 || sug_col < 0) {
        std::fprintf(stderr, "Error: the table has no course or suggested_course column. Run classify first.\n");
        return 1;
    }
    auto cell = [](const CsvRow& row, int col) {
        return col >= 0 && col < static_cast<int>(row.size()) ? row[static_cast<std::size_t>(col)] : std::string();
    };

    Tally overall;
    std::map<std::string, Tally> by_type;
    std::map<std::string, Tally> by_level;
    std::map<std::string, Tally> by_course;
    std::map<std::pair<std::string, std::string>, int> mixups;
    std::vector<std::string> wrong;
    int files = 0;
    int not_reviewed = 0;
    int no_suggestion = 0;
    for (std::size_t r = 1; r < rows.size(); ++r) {
        if (cell(rows[r], 0).size() != 64) {
            continue;
        }
        ++files;
        const std::string truth = cell(rows[r], course_col);
        const std::string guess = cell(rows[r], sug_col);
        if (truth.empty()) {
            ++not_reviewed;
            continue;
        }
        if (guess.empty()) {
            ++no_suggestion;  // reviewed, but the model gave nothing: counts as wrong for coverage
        }
        const bool ok = !guess.empty() && guess == truth;
        auto add = [ok](Tally& t) {
            t.correct += ok ? 1 : 0;
            ++t.total;
        };
        add(overall);
        add(by_type[cell(rows[r], type_col)]);
        add(by_level[guess.empty() ? "(none)" : cell(rows[r], level_col)]);
        add(by_course[truth]);
        if (!ok) {
            ++mixups[{truth, guess.empty() ? "(no suggestion)" : guess}];
            wrong.push_back(cell(rows[r], name_col) + ": " + (guess.empty() ? "(no suggestion)" : guess) + " -> " +
                            truth);
        }
    }

    std::printf("Files in table: %d   reviewed: %d   not reviewed: %d\n\n", files, overall.total, not_reviewed);
    if (overall.total == 0) {
        std::printf("Nothing reviewed yet. Open the review page, confirm the courses and save the table.\n");
        return 0;
    }
    std::printf("Accuracy over every reviewed file (files the model could not classify count as wrong):\n");
    print_line("all", overall);
    if (overall.total < 100) {
        std::printf("  Note: with fewer than 100 files the range is wide; treat the number as a first estimate.\n");
    }
    std::printf("\nBy file type:\n");
    for (const auto& [k, t] : by_type) {
        print_line(k, t);
    }
    std::printf("\nBy the model's own confidence:\n");
    for (const char* level : {"high", "medium", "low", "(none)"}) {
        if (by_level.count(level) != 0) {
            print_line(level, by_level[level]);
        }
    }
    const Tally& high = by_level["high"];
    if (high.total > 0) {
        std::printf("  If only \"high\" were accepted automatically: %.0f%% of files automated, %.1f%% of those correct.\n",
                    100.0 * high.total / overall.total, 100.0 * high.correct / high.total);
    }
    std::printf("\nBy course (the confirmed one):\n");
    for (const auto& [k, t] : by_course) {
        print_line(k, t);
    }
    if (!mixups.empty()) {
        std::vector<std::pair<int, std::pair<std::string, std::string>>> sorted;
        for (const auto& [pair, n] : mixups) {
            sorted.push_back({n, pair});
        }
        std::sort(sorted.rbegin(), sorted.rend());
        std::printf("\nMost common mix-ups (correct course <- suggested):\n");
        for (std::size_t i = 0; i < sorted.size() && i < 8; ++i) {
            std::printf("  %2d x  %s <- %s\n", sorted[i].first, sorted[i].second.first.c_str(),
                        sorted[i].second.second.c_str());
        }
        std::printf("\nWrong suggestions:\n");
        for (const std::string& w : wrong) {
            std::printf("  %s\n", w.c_str());
        }
    }
    return 0;
}

}  // namespace cli
