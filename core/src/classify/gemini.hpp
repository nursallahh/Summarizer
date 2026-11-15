#pragma once

// Course suggestion for one file with the Gemini API.
//
// The model only suggests. Every suggestion is shown to the user for review, and accuracy is
// measured against the user's reviewed labels, never against the model's own confidence.

#include "net/http.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace smz {

struct Course {
    std::string name;
    std::string description;  // optional: topics, terms, instructor; helps tell similar courses apart
};

// Values the model may return instead of a course name.
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
    long long tokens = 0;
};

enum class ApiError {
    None,
    RateLimited,   // too many requests per minute: wait and retry
    DailyQuota,    // free daily quota used up: stop and continue tomorrow
    BadKey,
    ModelNotFound,
    TooLarge,
    Blocked,       // the model refused this file (safety filter)
    Network,
    Server,        // 5xx: retry later
    BadResponse,
};

struct ClassifyResult {
    Suggestion suggestion;
    ApiError error = ApiError::None;
    std::string message;
    int retry_after_seconds = 0;
};

class GeminiClassifier {
public:
    GeminiClassifier(std::string api_key, std::string model, std::vector<Course> courses,
                     HttpTransport transport = http_post);

    ClassifyResult classify(const ClassifyInput& input) const;

    // Exposed for tests.
    std::string build_request_body(const ClassifyInput& input) const;
    ClassifyResult parse_response(int status, const std::string& body) const;

private:
    std::string api_key_;
    std::string model_;
    std::vector<Course> courses_;
    HttpTransport transport_;
};

std::string base64_encode(std::string_view data);

}  // namespace smz
