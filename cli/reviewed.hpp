#pragma once

// Finds a reviewed table that the review page saved somewhere else than the table itself.
//
// Browsers often save to the Downloads folder (as "dersnotlari-scan.csv" or
// "dersnotlari-scan (1).csv"). The newest such file that is newer than the table is used, so the
// person does not have to move it. Files are only read, never moved or deleted.

#include <filesystem>
#include <map>
#include <string>

namespace cli {

struct ReviewedLabels {
    std::string course;
    std::string notes;
};

struct ReviewedSave {
    std::filesystem::path file;                    // empty if none was found
    std::map<std::string, ReviewedLabels> by_sha;  // rows with a course or notes
};

ReviewedSave find_newer_reviewed_save(const std::filesystem::path& table);

}  // namespace cli
