// summarizer scan: builds the label table for the accuracy tests.
//
// It reads a folder of course files (read-only), and writes a CSV with one row per distinct file
// content. The person fills in the "course" column in Excel. Running scan again later keeps every
// course and note already written, and only adds rows for new files.

#include "scan.hpp"

#include "csv.hpp"
#include "json_reader.hpp"
#include "smz/smz_api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

namespace cli {

namespace {

fs::path path_from_utf8(const std::string& s) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(s.data()), s.size()));
}

std::string path_to_utf8(const fs::path& p) {
    const std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

// The label table contains personal course data, so it must never be written inside a git
// repository, where it could be committed by accident.
bool is_inside_git_repository(fs::path dir) {
    std::error_code ec;
    dir = fs::absolute(dir, ec);
    for (; !dir.empty(); dir = dir.parent_path()) {
        if (fs::exists(dir / ".git", ec)) {
            return true;
        }
        if (dir == dir.parent_path()) {
            break;
        }
    }
    return false;
}

// True if `child` is `parent` itself or somewhere below it (paths compared lexically).
bool is_within(const fs::path& child, const fs::path& parent) {
    const fs::path rel = child.lexically_normal().lexically_relative(parent.lexically_normal());
    return !rel.empty() && *rel.begin() != "..";
}

struct Labels {
    std::string course;
    std::string notes;
};

struct Row {
    std::string sha256;
    std::string file_name;
    std::vector<std::string> other_names;
    std::string type;
    std::int64_t size = 0;
    std::int64_t pages = -1;
    std::string captured_at;
    Labels labels;
    bool missing = false;
};

const std::vector<std::string> kHeader = {"sha256",  "file_name",   "other_names", "type",
                                          "size_kb", "pages",       "captured_at", "course",
                                          "notes"};

std::map<std::string, Labels> read_existing_labels(const fs::path& file, std::map<std::string, Row>& previous) {
    std::map<std::string, Labels> labels;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return labels;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    const std::vector<CsvRow> rows = parse_csv(to_utf8_if_needed(buffer.str()));
    if (rows.empty()) {
        return labels;
    }

    auto column = [&](const std::string& name) -> int {
        for (std::size_t i = 0; i < rows[0].size(); ++i) {
            if (rows[0][i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    const int sha_col = column("sha256");
    const int course_col = column("course");
    const int notes_col = column("notes");
    const int name_col = column("file_name");
    const int type_col = column("type");
    if (sha_col < 0) {
        return labels;
    }

    auto cell = [](const CsvRow& row, int col) { return col >= 0 && col < static_cast<int>(row.size()) ? row[col] : std::string(); };
    for (std::size_t r = 1; r < rows.size(); ++r) {
        const std::string sha = cell(rows[r], sha_col);
        if (sha.size() != 64) {
            continue;
        }
        Labels l{cell(rows[r], course_col), cell(rows[r], notes_col)};
        labels[sha] = l;
        Row row;
        row.sha256 = sha;
        row.file_name = cell(rows[r], name_col);
        row.type = cell(rows[r], type_col);
        row.labels = l;
        previous[sha] = row;
    }
    return labels;
}

// Runs the core's import job and collects one event per file.
bool import_folder(const std::string& folder_utf8, bool recursive, std::vector<JsonValue>& files,
                   std::string& finish_message) {
    smz_context* ctx = nullptr;
    if (smz_create(&ctx) != SMZ_OK) {
        std::fprintf(stderr, "Error: could not start the core.\n");
        return false;
    }

    std::int64_t job = 0;
    const smz_status started = smz_import_paths(ctx, folder_utf8.c_str(), recursive ? 1 : 0, &job);
    if (started != SMZ_OK) {
        std::fprintf(stderr, "Error: %s\n", smz_status_message(started));
        smz_destroy(ctx);
        return false;
    }

    bool finished = false;
    bool ok = false;
    while (!finished) {
        char* json = nullptr;
        if (smz_poll_events(ctx, 1000, &json) != SMZ_OK) {
            break;
        }
        const std::optional<JsonValue> batch = JsonReader::parse(json);
        smz_free_string(json);
        if (!batch) {
            std::fprintf(stderr, "Error: could not read events from the core.\n");
            break;
        }
        const JsonValue* events = batch->find("events");
        for (const JsonValue& e : events != nullptr ? events->array : std::vector<JsonValue>{}) {
            const std::string type = e.string_or("type");
            if (type == "file_imported") {
                files.push_back(e);
            } else if (type == "job_progress") {
                std::printf("\rScanning... %lld / %lld", static_cast<long long>(e.int_or("done")),
                            static_cast<long long>(e.int_or("total")));
                std::fflush(stdout);
            } else if (type == "job_finished") {
                finished = true;
                ok = e.string_or("outcome") == "succeeded";
                finish_message = e.string_or("message");
            }
        }
        if (!finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    std::printf("\n");
    smz_destroy(ctx);
    return ok;
}

}  // namespace

void print_scan_help() {
    std::printf(
        "summarizer scan <folder> [--out <file.csv>] [--no-recursive]\n"
        "\n"
        "  Reads every file in <folder> (read-only) and writes a CSV label table with one row\n"
        "  per distinct file content. Fill in the \"course\" column in Excel.\n"
        "  Running scan again keeps the courses and notes you already wrote.\n"
        "\n"
        "  --out <file.csv>   Where to write the table.\n"
        "                     Default: next to the folder, named <folder>-scan.csv\n"
        "  --no-recursive     Do not include sub-folders.\n"
        "\n"
        "  The table must be saved outside any git repository; scan refuses otherwise.\n");
}

int run_scan(const std::vector<std::string>& args) {
    std::string folder_arg;
    std::string out_arg;
    bool recursive = true;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--out" && i + 1 < args.size()) {
            out_arg = args[++i];
        } else if (args[i] == "--no-recursive") {
            recursive = false;
        } else if (args[i] == "--help" || args[i] == "-h") {
            print_scan_help();
            return 0;
        } else if (folder_arg.empty() && args[i].rfind("--", 0) != 0) {
            folder_arg = args[i];
        } else {
            std::fprintf(stderr, "Unknown option: %s\n\n", args[i].c_str());
            print_scan_help();
            return 2;
        }
    }
    if (folder_arg.empty()) {
        print_scan_help();
        return 2;
    }

    std::error_code ec;
    fs::path folder = fs::absolute(path_from_utf8(folder_arg), ec);
    if (!fs::is_directory(folder, ec)) {
        std::fprintf(stderr, "Error: folder not found: %s\n", folder_arg.c_str());
        return 1;
    }
    folder = folder.lexically_normal();
    if (folder.has_filename() == false) {
        folder = folder.parent_path();  // "C:\x\" -> "C:\x"
    }

    const fs::path out = out_arg.empty()
                             ? folder.parent_path() / path_from_utf8(path_to_utf8(folder.filename()) + "-scan.csv")
                             : fs::absolute(path_from_utf8(out_arg), ec);
    if (is_inside_git_repository(out.parent_path())) {
        std::fprintf(stderr,
                     "Error: %s is inside a git repository.\n"
                     "The label table contains personal course data; save it outside the repository,\n"
                     "for example with --out C:\\Users\\<you>\\Desktop\\summarizer-test\\labels.csv\n",
                     path_to_utf8(out).c_str());
        return 1;
    }
    if (is_within(out.parent_path(), folder)) {
        std::fprintf(stderr, "Error: do not write the table inside the scanned folder; it would be scanned next time.\n");
        return 1;
    }

    std::map<std::string, Row> previous;
    const std::map<std::string, Labels> labels = read_existing_labels(out, previous);
    if (!labels.empty()) {
        std::printf("Keeping labels from the existing table (%zu rows).\n", labels.size());
    }

    std::printf("Scanning %s\n", path_to_utf8(folder).c_str());
    std::vector<JsonValue> files;
    std::string finish_message;
    if (!import_folder(path_to_utf8(folder), recursive, files, finish_message)) {
        std::fprintf(stderr, "Error: scan did not finish. %s\n", finish_message.c_str());
        return 1;
    }

    // One row per distinct content. Files arrive sorted by path, so the first name is stable.
    std::map<std::string, Row> rows;
    std::map<std::string, int> by_type;
    std::vector<std::string> skipped;
    for (const JsonValue& f : files) {
        const std::string type = f.string_or("file_type");
        const std::string sha = f.string_or("sha256");
        if (type == "unsupported" || !f.string_or("error").empty() || sha.size() != 64) {
            skipped.push_back(f.string_or("name") + (f.string_or("error").empty() ? "" : " (" + f.string_or("error") + ")"));
            continue;
        }
        auto [it, inserted] = rows.try_emplace(sha);
        Row& row = it->second;
        if (inserted) {
            row.sha256 = sha;
            row.file_name = f.string_or("name");
            row.type = type;
            row.size = f.int_or("size");
            row.pages = f.int_or("pages", -1);
            row.captured_at = f.string_or("captured_at");
            const auto known = labels.find(sha);
            if (known != labels.end()) {
                row.labels = known->second;
            }
            ++by_type[type];
        } else {
            // Prefer the shorter name as the main one: "IMG_1.jpg" over "IMG_1 (1).jpg".
            std::string name = f.string_or("name");
            if (name.size() < row.file_name.size()) {
                std::swap(name, row.file_name);
            }
            row.other_names.push_back(std::move(name));
        }
    }

    // Keep labelled rows whose file is gone, so no work is lost.
    int missing = 0;
    for (auto& [sha, row] : previous) {
        if (rows.count(sha) == 0 && (!row.labels.course.empty() || !row.labels.notes.empty())) {
            row.missing = true;
            rows[sha] = row;
            ++missing;
        }
    }

    // Photos in capture order: consecutive photos usually belong to the same lecture,
    // which makes labelling faster. Then everything else by name.
    std::vector<const Row*> ordered;
    for (const auto& [sha, row] : rows) {
        ordered.push_back(&row);
    }
    std::sort(ordered.begin(), ordered.end(), [](const Row* a, const Row* b) {
        if (a->captured_at.empty() != b->captured_at.empty()) {
            return !a->captured_at.empty();
        }
        if (a->captured_at != b->captured_at) {
            return a->captured_at < b->captured_at;
        }
        return a->file_name < b->file_name;
    });

    const char sep = preferred_csv_separator();
    std::string csv = "\xEF\xBB\xBF";
    for (std::size_t i = 0; i < kHeader.size(); ++i) {
        csv += (i ? std::string(1, sep) : "") + kHeader[i];
    }
    csv += "\r\n";
    int unlabelled = 0;
    for (const Row* row : ordered) {
        std::string others;
        for (const std::string& n : row->other_names) {
            others += (others.empty() ? "" : " | ") + n;
        }
        std::string notes = row->labels.notes;
        if (row->missing && notes.find("[not found in last scan]") == std::string::npos) {
            notes = "[not found in last scan] " + notes;
        }
        const std::vector<std::string> fields = {
            row->sha256,
            row->file_name,
            others,
            row->type,
            row->missing ? "" : std::to_string((row->size + 1023) / 1024),
            row->pages >= 0 ? std::to_string(row->pages) : "",
            row->captured_at,
            row->labels.course,
            notes,
        };
        for (std::size_t i = 0; i < fields.size(); ++i) {
            csv += (i ? std::string(1, sep) : "") + csv_escape(fields[i], sep);
        }
        csv += "\r\n";
        unlabelled += row->labels.course.empty() && !row->missing ? 1 : 0;
    }

    // Write to a temporary file first, keep a backup of the old table, then replace it.
    fs::create_directories(out.parent_path(), ec);
    const fs::path tmp = path_from_utf8(path_to_utf8(out) + ".tmp");
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        file.write(csv.data(), static_cast<std::streamsize>(csv.size()));
        if (!file) {
            std::fprintf(stderr, "Error: could not write %s\n", path_to_utf8(tmp).c_str());
            return 1;
        }
    }
    if (fs::exists(out, ec)) {
        fs::copy_file(out, path_from_utf8(path_to_utf8(out) + ".bak"), fs::copy_options::overwrite_existing, ec);
    }
    fs::rename(tmp, out, ec);
    if (ec) {
        fs::remove(tmp, ec);
        std::fprintf(stderr, "Error: could not replace %s. Is it open in Excel? Close it and run scan again.\n",
                     path_to_utf8(out).c_str());
        return 1;
    }

    std::printf("\nWrote %s\n", path_to_utf8(out).c_str());
    std::printf("  %zu distinct files", rows.size() - static_cast<std::size_t>(missing));
    for (const auto& [type, count] : by_type) {
        std::printf(", %d %s", count, type.c_str());
    }
    std::printf("\n");
    const std::size_t duplicates = files.size() - skipped.size() - (rows.size() - static_cast<std::size_t>(missing));
    if (duplicates > 0) {
        std::printf("  %zu duplicate copies merged into one row (see other_names)\n", duplicates);
    }
    std::printf("  %d rows still need a course\n", unlabelled);
    if (missing > 0) {
        std::printf("  %d labelled files were not found this time; their rows were kept\n", missing);
    }
    if (!skipped.empty()) {
        std::printf("  %zu files skipped (unsupported type or unreadable):\n", skipped.size());
        for (const std::string& s : skipped) {
            std::printf("    %s\n", s.c_str());
        }
    }
    if (!finish_message.empty()) {
        std::printf("  Note: %s\n", finish_message.c_str());
    }
    return 0;
}

}  // namespace cli
