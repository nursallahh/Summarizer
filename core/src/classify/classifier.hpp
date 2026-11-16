#pragma once

// Course suggestions for files, with the Gemini API or a local model through Ollama.
//
// Models only suggest. Every suggestion is shown to the user for review, and accuracy is
// measured against the user's reviewed labels, never against the model's own confidence.

#include "net/http.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace smz {

struct Course {
    std::string name;
    std::string description;  // optional: topics, terms, instructor; helps tell similar courses apart
};

// Values a model may return instead of a course name.
inline constexpr std::string_view kNotCourseMaterial = "(not course material)";
inline constexpr std::string_view kCourseNotInList = "(course not in list)";
inline constexpr std::string_view kUnreadable = "(unreadable)";

struct ClassifyInput {
    std::string file_name;
    std::string mime_type;  // for binary content: image/jpeg, application/pdf, ...
    std::string bytes;      // binary content, sent inline
    std::string text;       // or extracted text (pptx, docx, txt)
};

struct Suggestion {
    std::string course;                     // a course name or one of the values above
    std::vector<std::string> other_courses; // other courses the file is also related to
    std::string level;                      // high, medium, low: the model's own estimate, not measured accuracy
    std::string kind;                       // notes, board, slides, exam_or_quiz, homework, reading, screenshot, other
    std::string topic;                      // main topic heading, in the file's language
    std::string evidence;                   // short reason
    long long tokens = 0;                   // for a batch: the whole request's tokens on the first file
};

enum class ApiError {
    None,
    RateLimited,   // too many requests per minute: wait and retry
    DailyQuota,    // daily quota used up: switch to the local model
    BadKey,
    ModelNotFound,
    TooLarge,
    Blocked,       // the model refused this file (safety filter)
    Network,       // no connection (or, for the local model, Ollama is not running)
    Server,        // 5xx: retry later
    BadResponse,
    Unsupported,   // this model cannot read this kind of file
};

struct ClassifyResult {
    Suggestion suggestion;
    ApiError error = ApiError::None;
    std::string message;
    int retry_after_seconds = 0;
};

class Classifier {
public:
    virtual ~Classifier() = default;

    // One result per input, in the same order. A request-level error is copied to every result.
    virtual std::vector<ClassifyResult> classify_batch(const std::vector<ClassifyInput>& inputs) const = 0;

    ClassifyResult classify(const ClassifyInput& input) const { return classify_batch({input}).front(); }

    // How many files and how many bytes of file content one request may carry.
    virtual std::size_t max_batch_files() const = 0;
    virtual std::size_t max_batch_bytes() const = 0;

    // Empty if the model can read this input, otherwise the reason it cannot.
    virtual std::string cannot_read(const ClassifyInput& input) const = 0;

    virtual std::string model() const = 0;
};

class GeminiClassifier : public Classifier {
public:
    GeminiClassifier(std::string api_key, std::string model, std::vector<Course> courses,
                     HttpTransport transport = http_post);

    std::vector<ClassifyResult> classify_batch(const std::vector<ClassifyInput>& inputs) const override;
    std::size_t max_batch_files() const override { return 10; }
    // Inline data per request (the request limit is ~20 MB as base64). A larger file is uploaded
    // with the Files API instead, sent alone, and deleted right after.
    std::size_t max_batch_bytes() const override { return 13u * 1024u * 1024u; }
    static constexpr std::size_t kUploadAbove = 8u * 1024u * 1024u;
    std::string cannot_read(const ClassifyInput& input) const override;
    std::string model() const override { return model_; }

    // Exposed for tests.
    // `uploaded[i]` is the Files API URI for inputs[i], or empty to send it inline.
    std::string build_request_body(const std::vector<ClassifyInput>& inputs,
                                   const std::vector<std::string>& uploaded = {}) const;
    std::vector<ClassifyResult> parse_response(int status, const std::string& body, std::size_t count) const;

private:
    struct Upload {
        std::string name;  // files/abc123
        std::string uri;
    };
    // Returns the upload, or an error result.
    std::optional<Upload> upload(const ClassifyInput& input, ClassifyResult& error) const;
    void delete_upload(const Upload& upload) const;

    std::string api_key_;
    std::string model_;
    std::vector<Course> courses_;
    HttpTransport transport_;
};

// A vision model on this computer, served by Ollama (https://ollama.com) on localhost.
// Reads one file per request; cannot read PDFs until PDF pages can be rendered to images.
class OllamaClassifier : public Classifier {
public:
    OllamaClassifier(std::string model, std::vector<Course> courses,
                     std::string base_url = "http://127.0.0.1:11434", HttpTransport transport = http_post);

    std::vector<ClassifyResult> classify_batch(const std::vector<ClassifyInput>& inputs) const override;
    std::size_t max_batch_files() const override { return 1; }
    std::size_t max_batch_bytes() const override { return 64u * 1024u * 1024u; }
    std::string cannot_read(const ClassifyInput& input) const override;
    std::string model() const override { return model_; }

    // Exposed for tests.
    std::string build_request_body(const ClassifyInput& input) const;
    ClassifyResult parse_response(int status, const std::string& body) const;

private:
    std::string model_;
    std::vector<Course> courses_;
    std::string base_url_;
    HttpTransport transport_;
};

std::string base64_encode(std::string_view data);

}  // namespace smz
