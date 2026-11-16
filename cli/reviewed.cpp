#include "reviewed.hpp"

#include "csv.hpp"

#if defined(_WIN32)
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#  include <knownfolders.h>
#  include <shlobj.h>
#endif

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace cli {

namespace {

fs::path downloads_folder() {
#if defined(_WIN32)
    PWSTR path = nullptr;
    fs::path result;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Downloads, 0, nullptr, &path)) && path != nullptr) {
        result = path;
    }
    CoTaskMemFree(path);
    return result;
#else
    const char* home = std::getenv("HOME");
    return home != nullptr ? fs::path(home) / "Downloads" : fs::path();
#endif
}

// "dersnotlari-scan.csv", "dersnotlari-scan (1).csv", "dersnotlari-scan(2).csv"
bool is_copy_of(const fs::path& candidate, const fs::path& table) {
    if (candidate.extension() != ".csv") {
        return false;
    }
    const std::u8string stem = table.stem().u8string();
    const std::u8string name = candidate.stem().u8string();
    if (name.rfind(stem, 0) != 0) {
        return false;
    }
    const std::u8string rest = name.substr(stem.size());
    if (rest.empty()) {
        return true;
    }
    std::size_t i = rest[0] == u8' ' ? 1 : 0;
    if (i >= rest.size() || rest[i] != u8'(' || rest.back() != u8')') {
        return false;
    }
    for (++i; i + 1 < rest.size(); ++i) {
        if (rest[i] < u8'0' || rest[i] > u8'9') {
            return false;
        }
    }
    return true;
}

}  // namespace

ReviewedSave find_newer_reviewed_save(const fs::path& table) {
    ReviewedSave save;
    std::error_code ec;
    const fs::file_time_type table_time = fs::last_write_time(table, ec);
    if (ec) {
        return save;  // no table yet
    }
    std::vector<fs::path> folders = {table.parent_path()};
    if (const fs::path d = downloads_folder(); !d.empty() && !fs::equivalent(d, table.parent_path(), ec)) {
        folders.push_back(d);
    }
    fs::file_time_type best_time = table_time;
    for (const fs::path& folder : folders) {
        for (fs::directory_iterator it(folder, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const fs::path p = it->path();
            if (!it->is_regular_file(ec) || fs::equivalent(p, table, ec) || !is_copy_of(p, table)) {
                continue;
            }
            const fs::file_time_type t = it->last_write_time(ec);
            if (!ec && t > best_time) {
                best_time = t;
                save.file = p;
            }
        }
        ec.clear();
    }
    if (save.file.empty()) {
        return save;
    }

    std::ifstream in(save.file, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::vector<CsvRow> rows = parse_csv(to_utf8_if_needed(buffer.str()));
    if (rows.empty()) {
        save.file.clear();
        return save;
    }
    int sha = -1;
    int course = -1;
    int notes = -1;
    for (std::size_t i = 0; i < rows[0].size(); ++i) {
        sha = rows[0][i] == "sha256" ? static_cast<int>(i) : sha;
        course = rows[0][i] == "course" ? static_cast<int>(i) : course;
        notes = rows[0][i] == "notes" ? static_cast<int>(i) : notes;
    }
    if (sha < 0 || course < 0) {
        save.file.clear();
        return save;
    }
    auto cell = [](const CsvRow& row, int col) {
        return col >= 0 && col < static_cast<int>(row.size()) ? row[static_cast<std::size_t>(col)] : std::string();
    };
    for (std::size_t r = 1; r < rows.size(); ++r) {
        ReviewedLabels l{cell(rows[r], course), cell(rows[r], notes)};
        const std::string key = cell(rows[r], sha);
        if (key.size() == 64 && (!l.course.empty() || !l.notes.empty())) {
            save.by_sha[key] = l;
        }
    }
    return save;
}

}  // namespace cli
