#include "smz/smz_api.h"

#include "event_queue.hpp"
#include "files/file_inspector.hpp"
#include "files/path_utf8.hpp"
#include "job_runner.hpp"
#include "json_writer.hpp"
#include "smz_version.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <new>
#include <set>
#include <string>
#include <system_error>
#include <vector>

// The opaque context handed to callers of the C API.
// Member order matters: the job runner is destroyed (and its threads joined) before the
// event queue it writes to.
struct smz_context {
    smz::EventQueue events;
    // Two workers for now; the right number is decided once OCR and model jobs exist (phase 0, step 4).
    smz::JobRunner jobs{events, 2};
};

namespace {

// Copies a std::string into a malloc-allocated C string that can cross the C boundary.
// It is released with smz_free_string (free); new/delete are never mixed with it.
char* copy_to_c_string(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

smz_status return_string(const std::string& value, char** out) {
    *out = copy_to_c_string(value);
    return *out != nullptr ? SMZ_OK : SMZ_ERR_OUT_OF_MEMORY;
}

// Runs `body` and converts any C++ exception into a status code.
// Exceptions must never cross the C boundary: Unity cannot catch them and would crash.
template <typename Body>
smz_status guarded(Body&& body) noexcept {
    try {
        return body();
    } catch (const std::bad_alloc&) {
        return SMZ_ERR_OUT_OF_MEMORY;
    } catch (...) {
        return SMZ_ERR_INTERNAL;
    }
}

std::string compiler_name() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown";
#endif
}

// Emits progress at roughly `steps_per_second`, independent of the OS sleep granularity
// (Windows sleeps are often ~15 ms): on every wake-up it emits all steps that are due.
smz::JobResult run_demo_job(smz::JobContext& job, std::int64_t total_steps, std::int64_t steps_per_second) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    std::int64_t done = 0;

    while (done < total_steps) {
        if (job.is_cancelled()) {
            return {smz::JobResult::Outcome::Cancelled, ""};
        }
        const auto elapsed_us =
            std::chrono::duration_cast<std::chrono::microseconds>(clock::now() - start).count();
        const std::int64_t due = std::min<std::int64_t>(total_steps, elapsed_us * steps_per_second / 1'000'000 + 1);
        while (done < due) {
            ++done;
            job.report_progress(done, total_steps);
        }
        if (done < total_steps && !job.sleep_for(std::chrono::milliseconds(5))) {
            return {smz::JobResult::Outcome::Cancelled, ""};
        }
    }
    return {smz::JobResult::Outcome::Succeeded, ""};
}


// Splits newline-separated UTF-8 paths, expands folders and returns a sorted, de-duplicated list.
std::vector<std::filesystem::path> expand_paths(const std::string& newline_separated, bool recursive,
                                                std::string& warnings) {
    namespace fs = std::filesystem;
    std::set<fs::path> files;
    std::size_t start = 0;
    while (start <= newline_separated.size()) {
        std::size_t end = newline_separated.find('\n', start);
        if (end == std::string::npos) {
            end = newline_separated.size();
        }
        std::string line = newline_separated.substr(start, end - start);
        start = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }

        const fs::path path = smz::path_from_utf8(line);
        std::error_code ec;
        if (fs::is_directory(path, ec)) {
            const auto options = fs::directory_options::skip_permission_denied;
            if (recursive) {
                for (fs::recursive_directory_iterator it(path, options, ec), last; !ec && it != last; it.increment(ec)) {
                    if (it->is_regular_file(ec)) {
                        files.insert(it->path());
                    }
                }
            } else {
                for (fs::directory_iterator it(path, options, ec), last; !ec && it != last; it.increment(ec)) {
                    if (it->is_regular_file(ec)) {
                        files.insert(it->path());
                    }
                }
            }
            if (ec) {
                warnings += "could not fully read folder " + line + ": " + ec.message() + "; ";
            }
        } else if (fs::is_regular_file(path, ec)) {
            files.insert(path);
        } else {
            warnings += "not found: " + line + "; ";
        }
    }
    return {files.begin(), files.end()};
}

smz::JobResult run_import_job(smz::JobContext& job, const std::string& paths, bool recursive) {
    std::string warnings;
    const std::vector<std::filesystem::path> files = expand_paths(paths, recursive, warnings);
    const auto total = static_cast<std::int64_t>(files.size());

    for (std::int64_t i = 0; i < total; ++i) {
        if (job.is_cancelled()) {
            return {smz::JobResult::Outcome::Cancelled, ""};
        }
        const smz::FileInfo info = smz::inspect_file(files[static_cast<std::size_t>(i)]);
        job.emit(smz::JsonObject()
                     .add("type", "file_imported")
                     .add("job", job.id())
                     .add("path", info.path)
                     .add("name", info.file_name)
                     .add("sha256", info.sha256)
                     .add("file_type", info.type)
                     .add("size", info.size)
                     .add("pages", std::int64_t{info.pages})
                     .add("captured_at", info.captured_at)
                     .add("error", info.error)
                     .finish());
        job.report_progress(i + 1, total);
    }
    return {smz::JobResult::Outcome::Succeeded, warnings};
}

}  // namespace

extern "C" {

const char* smz_version(void) {
    return SMZ_VERSION_STRING;
}

int32_t smz_abi_version(void) {
    return SMZ_ABI_VERSION;
}

const char* smz_status_message(smz_status status) {
    switch (status) {
        case SMZ_OK:
            return "Success";
        case SMZ_ERR_INVALID_ARGUMENT:
            return "Invalid argument";
        case SMZ_ERR_OUT_OF_MEMORY:
            return "Out of memory";
        case SMZ_ERR_INTERNAL:
            return "Unexpected internal error";
        case SMZ_ERR_NOT_FOUND:
            return "Not found";
    }
    return "Unknown status code";
}

smz_status smz_build_info(char** out_json) {
    if (out_json == nullptr) {
        return SMZ_ERR_INVALID_ARGUMENT;
    }
    *out_json = nullptr;
    return guarded([&] {
        const std::string json = smz::JsonObject()
                                     .add("version", SMZ_VERSION_STRING)
                                     .add("abi", std::int64_t{SMZ_ABI_VERSION})
                                     .add("compiler", compiler_name())
                                     .add("build_type", SMZ_BUILD_TYPE)
                                     .finish();
        return return_string(json, out_json);
    });
}

void smz_free_string(char* str) {
    std::free(str);
}

smz_status smz_create(smz_context** out_ctx) {
    if (out_ctx == nullptr) {
        return SMZ_ERR_INVALID_ARGUMENT;
    }
    *out_ctx = nullptr;
    return guarded([&] {
        *out_ctx = new smz_context();
        return SMZ_OK;
    });
}

void smz_destroy(smz_context* ctx) {
    // The destructor cancels jobs and joins worker threads; it does not throw.
    delete ctx;
}

smz_status smz_poll_events(smz_context* ctx, int32_t max_events, char** out_json) {
    if (ctx == nullptr || out_json == nullptr || max_events <= 0) {
        return SMZ_ERR_INVALID_ARGUMENT;
    }
    *out_json = nullptr;
    return guarded([&] {
        auto [events, dropped] = ctx->events.drain(static_cast<std::size_t>(max_events));

        std::string json = "{\"events\":[";
        for (std::size_t i = 0; i < events.size(); ++i) {
            if (i > 0) {
                json += ',';
            }
            json += events[i];
        }
        json += "],\"dropped\":";
        json += std::to_string(dropped);
        json += ",\"pending\":";
        json += std::to_string(ctx->events.size());
        json += '}';
        return return_string(json, out_json);
    });
}

smz_status smz_start_demo_job(smz_context* ctx, int32_t total_steps, int32_t steps_per_second,
                              int64_t* out_job_id) {
    if (ctx == nullptr || out_job_id == nullptr || total_steps <= 0 || total_steps > 10'000'000 ||
        steps_per_second <= 0 || steps_per_second > 1'000'000) {
        return SMZ_ERR_INVALID_ARGUMENT;
    }
    *out_job_id = 0;
    return guarded([&] {
        const std::int64_t total = total_steps;
        const std::int64_t rate = steps_per_second;
        *out_job_id = ctx->jobs.submit("demo", [total, rate](smz::JobContext& job) {
            return run_demo_job(job, total, rate);
        });
        return SMZ_OK;
    });
}

smz_status smz_cancel_job(smz_context* ctx, int64_t job_id) {
    if (ctx == nullptr) {
        return SMZ_ERR_INVALID_ARGUMENT;
    }
    return guarded([&] { return ctx->jobs.cancel(job_id) ? SMZ_OK : SMZ_ERR_NOT_FOUND; });
}

smz_status smz_import_paths(smz_context* ctx, const char* paths, int32_t recursive, int64_t* out_job_id) {
    if (ctx == nullptr || paths == nullptr || out_job_id == nullptr) {
        return SMZ_ERR_INVALID_ARGUMENT;
    }
    *out_job_id = 0;
    return guarded([&] {
        std::string copy(paths);  // the caller's buffer may be freed as soon as we return
        const bool deep = recursive != 0;
        *out_job_id = ctx->jobs.submit("import", [copy = std::move(copy), deep](smz::JobContext& job) {
            return run_import_job(job, copy, deep);
        });
        return SMZ_OK;
    });
}

}  // extern "C"
