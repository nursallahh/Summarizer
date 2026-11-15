#pragma once

// A single self-contained HTML page to review course suggestions in the browser.
// It shows each file with a preview, groups files by suggested course, and saves the reviewed
// table (same columns and format as the CSV) back to disk.
// Temporary phase 0 tool; the Unity app gets the real review screen.

#include <string>
#include <vector>

namespace cli {

struct ReviewRow {
    std::vector<std::string> fields;  // in header order
    std::string path;                 // UTF-8 path of the file for the preview, may be empty
};

struct ReviewData {
    std::string title;
    std::string table_file_name;
    char separator = ',';
    std::vector<std::string> header;
    std::vector<std::string> courses;
    std::vector<std::string> special;  // "(not course material)" etc.
    std::vector<ReviewRow> rows;
};

std::string build_review_page(const ReviewData& data);

}  // namespace cli
