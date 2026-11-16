// summarizer scan / classify: builds the label table for the accuracy tests.
//
// scan      reads a folder of course files (read-only) and writes a CSV with one row per
//           distinct file content.
// classify  does the same, then asks the model for a course suggestion for each file that has
//           none yet, and writes a review page where the person confirms or corrects them.
//
// Running either command again keeps everything already in the table and only adds what is new.

#include "scan.hpp"

#include "classify/classifier.hpp"
#include "csv.hpp"
#include "files/image_prep.hpp"
#include "files/office_text.hpp"
#include "json_reader.hpp"
#include "review_page.hpp"
#include "smz/smz_api.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

using smz::JsonReader;
using smz::JsonValue;

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

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

// Columns computed from the file on every scan.
const std::vector<std::string> kFileColumns = {"sha256", "file_name", "other_names", "type",
                                               "size_kb", "pages", "captured_at"};
// Columns kept from the previous table: the person's labels and the model's suggestions.
const std::vector<std::string> kKeptColumns = {"course",          "notes", "suggested_course", "also_related",
                                               "suggestion_level", "kind",  "topic",            "evidence",
                                               "model",           "classify_error"};

struct Row {
    std::string sha256;
    std::string path;  // UTF-8, empty for rows whose file was not found
    std::string file_name;
    std::vector<std::string> other_names;
    std::string type;
    std::int64_t size = 0;
    std::int64_t pages = -1;
    std::string captured_at;
    std::map<std::string, std::string> kept;
    bool missing = false;

    std::string get(const std::string& column) const {
        const auto it = kept.find(column);
        return it == kept.end() ? std::string() : it->second;
    }
};

std::map<std::string, Row> read_existing_table(const fs::path& file) {
    std::map<std::string, Row> previous;
    if (!fs::exists(file)) {
        return previous;
    }
    const std::vector<CsvRow> rows = parse_csv(to_utf8_if_needed(read_file(file)));
    if (rows.empty()) {
        return previous;
    }
    auto column = [&](const std::string& name) -> int {
        for (std::size_t i = 0; i < rows[0].size(); ++i) {
            if (rows[0][i] == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    };
    auto cell = [](const CsvRow& row, int col) {
        return col >= 0 && col < static_cast<int>(row.size()) ? row[static_cast<std::size_t>(col)] : std::string();
    };
    const int sha_col = column("sha256");
    if (sha_col < 0) {
        return previous;
    }
    for (std::size_t r = 1; r < rows.size(); ++r) {
        Row row;
        row.sha256 = cell(rows[r], sha_col);
        if (row.sha256.size() != 64) {
            continue;
        }
        row.file_name = cell(rows[r], column("file_name"));
        row.type = cell(rows[r], column("type"));
        for (const std::string& name : kKeptColumns) {
            const std::string value = cell(rows[r], column(name));
            if (!value.empty()) {
                row.kept[name] = value;
            }
        }
        previous[row.sha256] = row;
    }
    return previous;
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

std::vector<const Row*> ordered_rows(const std::map<std::string, Row>& rows) {
    // Photos in capture order: consecutive photos usually belong to the same lecture.
    // Then everything else by name.
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
    return ordered;
}

std::vector<std::string> table_header() {
    std::vector<std::string> header = kFileColumns;
    header.insert(header.end(), kKeptColumns.begin(), kKeptColumns.end());
    return header;
}

std::vector<std::string> table_fields(const Row& row) {
    std::string others;
    for (const std::string& n : row.other_names) {
        others += (others.empty() ? "" : " | ") + n;
    }
    std::vector<std::string> fields = {
        row.sha256,
        row.file_name,
        others,
        row.type,
        row.missing ? "" : std::to_string((row.size + 1023) / 1024),
        row.pages >= 0 ? std::to_string(row.pages) : "",
        row.captured_at,
    };
    for (const std::string& name : kKeptColumns) {
        std::string value = row.get(name);
        if (name == "notes" && row.missing && value.find("[not found in last scan]") == std::string::npos) {
            value = "[not found in last scan] " + value;
        }
        fields.push_back(value);
    }
    return fields;
}

// Writes to a temporary file, then replaces the table. Returns false if the table is locked.
bool write_table(const fs::path& out, const std::map<std::string, Row>& rows, char sep) {
    std::string csv = "\xEF\xBB\xBF";
    const std::vector<std::string> header = table_header();
    for (std::size_t i = 0; i < header.size(); ++i) {
        csv += (i ? std::string(1, sep) : "") + header[i];
    }
    csv += "\r\n";
    for (const Row* row : ordered_rows(rows)) {
        const std::vector<std::string> fields = table_fields(*row);
        for (std::size_t i = 0; i < fields.size(); ++i) {
            csv += (i ? std::string(1, sep) : "") + csv_escape(fields[i], sep);
        }
        csv += "\r\n";
    }
    std::error_code ec;
    fs::create_directories(out.parent_path(), ec);
    const fs::path tmp = path_from_utf8(path_to_utf8(out) + ".tmp");
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        file.write(csv.data(), static_cast<std::streamsize>(csv.size()));
        if (!file) {
            return false;
        }
    }
    fs::rename(tmp, out, ec);
    if (ec) {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

// ---- classification ----

struct ClassifyOptions {
    bool enabled = false;
    std::string mode = "auto";  // auto: Gemini, then the local model; gemini; local
    std::string model = "gemini-3.5-flash-lite";
    std::string local_model = "qwen3-vl:8b";
    std::string courses_file;
    int requests_per_minute = 0;  // 0: no fixed limit, only wait when the service asks to
    bool redo = false;
};

std::vector<smz::Course> read_courses(const fs::path& file) {
    std::vector<smz::Course> courses;
    std::string text = to_utf8_if_needed(read_file(file));
    if (text.rfind("\xEF\xBB\xBF", 0) == 0) {
        text.erase(0, 3);
    }
    std::istringstream lines(text);
    std::string line;
    auto trim = [](std::string s) {
        const auto b = s.find_first_not_of(" \t\r");
        const auto e = s.find_last_not_of(" \t\r");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        smz::Course c;
        const auto bar = line.find('|');
        c.name = trim(line.substr(0, bar));
        if (bar != std::string::npos) {
            c.description = trim(line.substr(bar + 1));
        }
        if (!c.name.empty()) {
            courses.push_back(c);
        }
    }
    return courses;
}

// Reads a file into a model input. Photos are rotated upright and scaled to 1600 px, which keeps
// writing readable and makes requests small. Returns an empty error on success.
std::string prepare_input(const Row& row, smz::ClassifyInput& input) {
    constexpr std::size_t kMaxTextChars = 40000;
    input.file_name = row.file_name;
    const fs::path path = path_from_utf8(row.path);
    static const std::map<std::string, std::string> kImageMime = {
        {"jpeg", "image/jpeg"}, {"png", "image/png"}, {"webp", "image/webp"}, {"heic", "image/heic"}};
    if (const auto it = kImageMime.find(row.type); it != kImageMime.end()) {
        if (auto image = smz::prepare_image(path, 1600, 85)) {
            input.mime_type = "image/jpeg";
            input.bytes = std::move(image->jpeg);
            return "";
        }
        input.mime_type = it->second;  // could not convert: send the original
        input.bytes = read_file(path);
        return input.bytes.empty() ? "could not read the file" : "";
    }
    if (row.type == "pdf") {
        input.mime_type = "application/pdf";
        input.bytes = read_file(path);
        return input.bytes.empty() ? "could not read the file" : "";
    }
    if (row.type == "pptx" || row.type == "docx") {
        const smz::OfficeText office = smz::extract_office_text(path, kMaxTextChars);
        if (!office.error.empty()) {
            return "could not read the document: " + office.error;
        }
        bool has_text = false;
        std::istringstream lines(office.text);
        for (std::string line; std::getline(lines, line);) {
            if (line.rfind("--- Slide ", 0) != 0 && line.find_first_not_of(" \t\r") != std::string::npos) {
                has_text = true;
                break;
            }
        }
        if (!has_text) {
            return "the document has no text (slides with only pictures are not supported yet)";
        }
        input.text = office.text;
        return "";
    }
    if (row.type == "text") {
        input.text = read_file(path).substr(0, kMaxTextChars);
        return "";
    }
    return row.type + " files are not supported for classification yet";
}

std::string read_api_key() {
    std::string key;
#if defined(_WIN32)
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, "GEMINI_API_KEY") == 0 && value != nullptr) {
        key = value;
    }
    std::free(value);
#else
    if (const char* value = std::getenv("GEMINI_API_KEY")) {
        key = value;
    }
#endif
    return key;
}

void print_ollama_help(const std::string& model) {
    std::fprintf(stderr,
                 "To use the local model:\n"
                 "  1. Install Ollama from https://ollama.com/download and start it.\n"
                 "  2. In a terminal run:  ollama pull %s\n"
                 "  3. Run this command again; it continues where it stopped.\n",
                 model.c_str());
}

// Classifies rows that have no suggestion yet, several files per request. Uses Gemini first and
// switches to the local model when the daily quota is used up, the key is rejected or there is no
// internet. Saves the table after every request so nothing is lost if the run stops.
// Returns false if the run had to stop early.
bool classify_rows(std::map<std::string, Row>& rows, const ClassifyOptions& options, const fs::path& out,
                   char sep) {
    const fs::path courses_path = options.courses_file.empty() ? out.parent_path() / "courses.txt"
                                                               : path_from_utf8(options.courses_file);
    const std::vector<smz::Course> courses = read_courses(courses_path);
    if (courses.size() < 2) {
        std::fprintf(stderr,
                     "Error: %s must list at least two courses, one per line:\n"
                     "  Course name | optional description (topics, terms, instructor)\n",
                     path_to_utf8(courses_path).c_str());
        return false;
    }
    std::printf("Courses (%zu) from %s\n", courses.size(), path_to_utf8(courses_path).c_str());

    std::unique_ptr<smz::Classifier> gemini;
    std::unique_ptr<smz::Classifier> local;
    const bool local_allowed = options.mode != "gemini";
    if (local_allowed) {
        local = std::make_unique<smz::OllamaClassifier>(options.local_model, courses);
    }
    if (options.mode != "local") {
        const std::string key = read_api_key();
        if (!key.empty()) {
            gemini = std::make_unique<smz::GeminiClassifier>(key, options.model, courses);
        } else if (!local_allowed) {
            std::fprintf(stderr,
                         "Error: no Gemini API key. Create one at https://aistudio.google.com/apikey, then run\n"
                         "  setx GEMINI_API_KEY \"<your key>\"\n"
                         "and open a new terminal.\n");
            return false;
        } else {
            std::printf("No GEMINI_API_KEY found; using the local model.\n");
        }
    }
    const smz::Classifier* current = gemini ? gemini.get() : local.get();
    auto model_label = [&](const smz::Classifier* c) {
        return c == local.get() ? "local:" + c->model() : c->model();
    };
    auto switch_to_local = [&](const std::string& why) {
        if (!local_allowed || current == local.get()) {
            return false;
        }
        std::printf("\n%s -> continuing with the local model (%s via Ollama).\n", why.c_str(),
                    options.local_model.c_str());
        current = local.get();
        return true;
    };

    std::vector<Row*> todo;
    for (const Row* r : ordered_rows(rows)) {
        Row* row = &rows[r->sha256];
        if (!row->missing && (options.redo || row->get("suggested_course").empty())) {
            todo.push_back(row);
        }
    }
    if (todo.empty()) {
        std::printf("Every file already has a suggestion. Use --redo to ask again.\n");
        return true;
    }
    std::printf("%zu files to classify with %s.\n", todo.size(), model_label(current).c_str());

    const auto started = std::chrono::steady_clock::now();
    const auto min_interval = options.requests_per_minute > 0
                                  ? std::chrono::milliseconds(60000 / options.requests_per_minute)
                                  : std::chrono::milliseconds(0);
    auto next_request = std::chrono::steady_clock::now();
    std::map<Row*, smz::ClassifyInput> prepared;  // kept when switching models
    long long tokens = 0;
    int done = 0;
    int failed = 0;
    std::size_t i = 0;
    while (i < todo.size()) {
        // Build the next request: consecutive files (photos in capture order) up to the model's limits.
        std::vector<Row*> batch_rows;
        std::vector<smz::ClassifyInput> batch;
        std::size_t batch_bytes = 0;
        std::size_t j = i;
        for (; j < todo.size() && batch.size() < current->max_batch_files(); ++j) {
            Row* row = todo[j];
            auto it = prepared.find(row);
            if (it == prepared.end()) {
                smz::ClassifyInput input;
                if (const std::string error = prepare_input(*row, input); !error.empty()) {
                    row->kept["classify_error"] = error;
                    ++failed;
                    std::printf("  %s: skipped, %s\n", row->file_name.c_str(), error.c_str());
                    prepared[row] = {};
                    continue;
                }
                it = prepared.emplace(row, std::move(input)).first;
            }
            if (it->second.file_name.empty()) {
                continue;  // could not be prepared
            }
            const std::size_t size = it->second.bytes.size() + it->second.text.size();
            if (const std::string reason = current->cannot_read(it->second); !reason.empty()) {
                row->kept["classify_error"] = reason;
                ++failed;
                std::printf("  %s: skipped, %s\n", row->file_name.c_str(), reason.c_str());
                continue;
            }
            if (!batch.empty() && batch_bytes + size > current->max_batch_bytes()) {
                break;
            }
            batch_rows.push_back(row);
            batch.push_back(it->second);
            batch_bytes += size;
        }
        if (batch.empty()) {
            i = j;
            continue;
        }

        std::printf("[%zu-%zu / %zu] %s, %zu file%s (%.1f MB)... ", i + 1, j, todo.size(),
                    model_label(current).c_str(), batch.size(), batch.size() == 1 ? "" : "s",
                    static_cast<double>(batch_bytes) / (1024.0 * 1024.0));
        std::fflush(stdout);

        std::vector<smz::ClassifyResult> results;
        for (int attempt = 0; attempt < 6; ++attempt) {
            std::this_thread::sleep_until(next_request);
            next_request = std::chrono::steady_clock::now() + min_interval;
            results = current->classify_batch(batch);
            const smz::ApiError e = results.front().error;
            const bool all_same = std::all_of(results.begin(), results.end(),
                                              [&](const smz::ClassifyResult& r) { return r.error == e; });
            if (!all_same || (e != smz::ApiError::RateLimited && e != smz::ApiError::Server &&
                              !(e == smz::ApiError::Network && attempt == 0))) {
                break;
            }
            const int wait = results.front().retry_after_seconds > 0 ? results.front().retry_after_seconds
                                                                      : 5 * (attempt + 1);
            std::printf("busy, waiting %d s... ", wait);
            std::fflush(stdout);
            next_request = std::chrono::steady_clock::now() + std::chrono::seconds(wait);
        }
        std::printf("\n");

        // Request-level problems: switch models or stop.
        const smz::ClassifyResult& first = results.front();
        const bool request_failed = std::all_of(results.begin(), results.end(),
                                                [&](const smz::ClassifyResult& r) { return r.error == first.error; }) &&
                                    first.error != smz::ApiError::None;
        if (request_failed) {
            const std::string why = first.message;
            if (current == gemini.get()) {
                if (first.error == smz::ApiError::DailyQuota && switch_to_local("The free daily Gemini quota is used up")) {
                    continue;
                }
                if (first.error == smz::ApiError::Network && switch_to_local("No internet connection")) {
                    continue;
                }
                if (first.error == smz::ApiError::BadKey && switch_to_local("The Gemini API key was rejected")) {
                    continue;
                }
                if (first.error == smz::ApiError::RateLimited && switch_to_local("Gemini keeps refusing requests")) {
                    continue;
                }
            }
            const bool fatal = first.error == smz::ApiError::DailyQuota || first.error == smz::ApiError::Network ||
                               first.error == smz::ApiError::BadKey || first.error == smz::ApiError::ModelNotFound ||
                               first.error == smz::ApiError::RateLimited || first.error == smz::ApiError::Server;
            if (fatal) {
                write_table(out, rows, sep);
                std::fprintf(stderr, "\nStopped: %s\n", why.c_str());
                if (current == local.get()) {
                    print_ollama_help(options.local_model);
                } else if (first.error == smz::ApiError::ModelNotFound) {
                    std::fprintf(stderr, "Try another model with --model, for example --model gemini-2.5-flash-lite\n");
                } else if (first.error == smz::ApiError::BadKey) {
                    std::fprintf(stderr, "Check GEMINI_API_KEY.\n");
                }
                std::fprintf(stderr, "Everything done so far is saved; run the same command again to continue.\n");
                return false;
            }
        }

        for (std::size_t k = 0; k < batch.size(); ++k) {
            Row& row = *batch_rows[k];
            const smz::ClassifyResult& r = results[k];
            if (r.error == smz::ApiError::None) {
                const smz::Suggestion& s = r.suggestion;
                std::string also;
                for (const std::string& o : s.other_courses) {
                    also += (also.empty() ? "" : " | ") + o;
                }
                row.kept["suggested_course"] = s.course;
                row.kept["also_related"] = also;
                row.kept["suggestion_level"] = s.level;
                row.kept["kind"] = s.kind;
                row.kept["topic"] = s.topic;
                row.kept["evidence"] = s.evidence;
                row.kept["model"] = model_label(current);
                row.kept.erase("classify_error");
                tokens += s.tokens;
                ++done;
                std::printf("  %s: %s%s\n", row.file_name.c_str(), s.course.c_str(),
                            s.level == "high" ? "" : (" (" + s.level + ")").c_str());
            } else {
                row.kept["classify_error"] = r.message;
                ++failed;
                std::printf("  %s: failed, %s\n", row.file_name.c_str(), r.message.c_str());
            }
        }
        if (!write_table(out, rows, sep)) {
            std::fprintf(stderr, "Error: could not save the table. Is it open in Excel? Close it and run again.\n");
            return false;
        }
        i = j;
    }
    failed = static_cast<int>(std::count_if(todo.begin(), todo.end(), [](const Row* r) {
        return !r->get("classify_error").empty() && r->get("suggested_course").empty();
    }));
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - started).count();
    std::printf("\nSuggestions: %d new, %d could not be classified (see classify_error). %lld s, %lld tokens.\n", done,
                failed, static_cast<long long>(seconds), tokens);
    return true;
}

void write_review(const fs::path& out, const std::map<std::string, Row>& rows, char sep) {
    std::vector<std::string> courses;
    {
        // The courses the person can choose from: the course list plus any course already used.
        const fs::path courses_path = out.parent_path() / "courses.txt";
        for (const smz::Course& c : read_courses(courses_path)) {
            courses.push_back(c.name);
        }
        for (const auto& [sha, row] : rows) {
            for (const std::string& name : {row.get("course"), row.get("suggested_course")}) {
                if (!name.empty() && name[0] != '(' &&
                    std::find(courses.begin(), courses.end(), name) == courses.end()) {
                    courses.push_back(name);
                }
            }
        }
    }
    ReviewData data;
    data.title = path_to_utf8(out.stem());
    data.table_file_name = path_to_utf8(out.filename());
    data.separator = sep;
    data.header = table_header();
    data.courses = courses;
    data.special = {std::string(smz::kCourseNotInList), std::string(smz::kNotCourseMaterial),
                    std::string(smz::kUnreadable)};
    for (const Row* row : ordered_rows(rows)) {
        ReviewRow r;
        r.fields = table_fields(*row);
        r.path = row->path;
        data.rows.push_back(std::move(r));
    }
    const fs::path page = path_from_utf8(path_to_utf8(out.parent_path() / out.stem()) + "-review.html");
    std::ofstream file(page, std::ios::binary | std::ios::trunc);
    const std::string html = build_review_page(data);
    file.write(html.data(), static_cast<std::streamsize>(html.size()));
    if (file) {
        std::printf("Review page: %s\n", path_to_utf8(page).c_str());
    }
}

}  // namespace

void print_scan_help() {
    std::printf(
        "summarizer scan <folder> [--out <file.csv>] [--no-recursive]\n"
        "summarizer classify <folder> [--out <file.csv>] [--courses <courses.txt>] [--model <id>]\n"
        "                    [--local-model <name>] [--only gemini|local] [--rpm <n>] [--redo]\n"
        "                    [--no-recursive]\n"
        "\n"
        "  scan reads every file in <folder> (read-only) and writes a CSV table with one row\n"
        "  per distinct file content.\n"
        "\n"
        "  classify does the same, then suggests a course for each file that has none yet, and\n"
        "  writes <table>-review.html next to the table. Open it in a browser, correct the wrong\n"
        "  suggestions, and save the reviewed table over the old one.\n"
        "  It uses Gemini (GEMINI_API_KEY) and sends several files per request. When the daily\n"
        "  quota is used up or there is no internet, it continues with a local model via Ollama.\n"
        "  Needs courses.txt next to the table: one course per line,\n"
        "  \"Course name | optional description\".\n"
        "\n"
        "  Running either command again keeps courses, notes and suggestions already in the table.\n"
        "\n"
        "  --out <file.csv>      Where to write the table.\n"
        "                        Default: next to the folder, named <folder>-scan.csv\n"
        "  --courses <file>      Course list. Default: courses.txt next to the table.\n"
        "  --model <id>          Gemini model. Default: gemini-3.5-flash-lite\n"
        "  --local-model <name>  Ollama model. Default: qwen3-vl:8b\n"
        "  --only gemini|local   Use only one of the two.\n"
        "  --rpm <n>             At most n requests per minute (default: as fast as allowed).\n"
        "  --redo                Ask again for files that already have a suggestion.\n"
        "  --no-recursive        Do not include sub-folders.\n"
        "\n"
        "  The table must be saved outside any git repository; the command refuses otherwise.\n");
}

int run_scan(const std::vector<std::string>& args, bool classify) {
    std::string folder_arg;
    std::string out_arg;
    bool recursive = true;
    ClassifyOptions options;
    options.enabled = classify;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const bool has_value = i + 1 < args.size();
        if (args[i] == "--out" && has_value) {
            out_arg = args[++i];
        } else if (args[i] == "--no-recursive") {
            recursive = false;
        } else if (classify && args[i] == "--courses" && has_value) {
            options.courses_file = args[++i];
        } else if (classify && args[i] == "--model" && has_value) {
            options.model = args[++i];
        } else if (classify && args[i] == "--local-model" && has_value) {
            options.local_model = args[++i];
        } else if (classify && args[i] == "--only" && has_value) {
            options.mode = args[++i];
            if (options.mode != "gemini" && options.mode != "local") {
                std::fprintf(stderr, "--only must be gemini or local\n");
                return 2;
            }
        } else if (classify && args[i] == "--rpm" && has_value) {
            options.requests_per_minute = std::max(0, std::atoi(args[++i].c_str()));
        } else if (classify && args[i] == "--redo") {
            options.redo = true;
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
    if (!folder.has_filename()) {
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

    std::map<std::string, Row> previous = read_existing_table(out);
    if (!previous.empty()) {
        std::printf("Keeping labels and suggestions from the existing table (%zu rows).\n", previous.size());
    }

    std::printf("Scanning %s\n", path_to_utf8(folder).c_str());
    std::vector<JsonValue> files;
    std::string finish_message;
    if (!import_folder(path_to_utf8(folder), recursive, files, finish_message)) {
        std::fprintf(stderr, "Error: scan did not finish. %s\n", finish_message.c_str());
        return 1;
    }

    // One row per distinct content. Files arrive sorted by path.
    std::map<std::string, Row> rows;
    std::map<std::string, int> by_type;
    std::vector<std::string> skipped;
    std::size_t imported = 0;
    for (const JsonValue& f : files) {
        const std::string type = f.string_or("file_type");
        const std::string sha = f.string_or("sha256");
        const std::string error = f.string_or("error");
        if (type == "unsupported" || !error.empty() || sha.size() != 64) {
            skipped.push_back(f.string_or("name") + (error.empty() ? "" : ": " + error));
            continue;
        }
        ++imported;
        auto [it, inserted] = rows.try_emplace(sha);
        Row& row = it->second;
        if (inserted) {
            row.sha256 = sha;
            row.path = f.string_or("path");
            row.file_name = f.string_or("name");
            row.type = type;
            row.size = f.int_or("size");
            row.pages = f.int_or("pages", -1);
            row.captured_at = f.string_or("captured_at");
            const auto known = previous.find(sha);
            if (known != previous.end()) {
                row.kept = known->second.kept;
            }
            ++by_type[type];
        } else {
            // Prefer the shorter name as the main one: "IMG_1.jpg" over "IMG_1 (1).jpg".
            std::string name = f.string_or("name");
            if (name.size() < row.file_name.size()) {
                std::swap(name, row.file_name);
                row.path = f.string_or("path");
            }
            row.other_names.push_back(std::move(name));
        }
    }
    const std::size_t distinct = rows.size();

    // Keep labelled rows whose file is gone, so no work is lost.
    int missing = 0;
    for (auto& [sha, row] : previous) {
        if (rows.count(sha) == 0 && (!row.get("course").empty() || !row.get("notes").empty())) {
            row.missing = true;
            rows[sha] = row;
            ++missing;
        }
    }

    const char sep = preferred_csv_separator();
    if (fs::exists(out, ec)) {
        fs::copy_file(out, path_from_utf8(path_to_utf8(out) + ".bak"), fs::copy_options::overwrite_existing, ec);
    }
    if (!write_table(out, rows, sep)) {
        std::fprintf(stderr, "Error: could not write %s. Is it open in Excel? Close it and run again.\n",
                     path_to_utf8(out).c_str());
        return 1;
    }

    std::printf("Wrote %s\n", path_to_utf8(out).c_str());
    std::printf("  %zu distinct files", distinct);
    for (const auto& [type, count] : by_type) {
        std::printf(", %d %s", count, type.c_str());
    }
    std::printf("\n");
    if (imported > distinct) {
        std::printf("  %zu duplicate copies merged into one row (see other_names)\n", imported - distinct);
    }
    if (missing > 0) {
        std::printf("  %d labelled files were not found this time; their rows were kept\n", missing);
    }
    if (!skipped.empty()) {
        std::printf("  %zu files skipped:\n", skipped.size());
        for (const std::string& s : skipped) {
            std::printf("    %s\n", s.c_str());
        }
    }
    if (!finish_message.empty()) {
        std::printf("  Note: %s\n", finish_message.c_str());
    }

    if (!options.enabled) {
        return 0;
    }
    std::printf("\n");
    const bool completed = classify_rows(rows, options, out, sep);
    write_review(out, rows, sep);
    return completed ? 0 : 1;
}

}  // namespace cli
