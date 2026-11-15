#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace smz {

// Minimal JSON helpers for the C API. Only what the core needs to *write* events;
// a full JSON library is added when the core starts *reading* JSON.

// Appends `value` as a quoted JSON string, escaping as required by RFC 8259.
inline void append_json_string(std::string& out, std::string_view value) {
    static constexpr char kHex[] = "0123456789abcdef";
    out += '"';
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0x0F];
                } else {
                    out += ch;  // UTF-8 bytes pass through unchanged
                }
        }
    }
    out += '"';
}

// Small builder for a single flat JSON object: {"a":1,"b":"x"}.
class JsonObject {
public:
    JsonObject() { text_ += '{'; }

    JsonObject& add(std::string_view key, std::string_view value) {
        key_(key);
        append_json_string(text_, value);
        return *this;
    }

    JsonObject& add(std::string_view key, const char* value) {
        return add(key, std::string_view(value));
    }

    JsonObject& add(std::string_view key, std::int64_t value) {
        key_(key);
        text_ += std::to_string(value);
        return *this;
    }

    std::string finish() {
        text_ += '}';
        return std::move(text_);
    }

private:
    void key_(std::string_view key) {
        if (!first_) {
            text_ += ',';
        }
        first_ = false;
        append_json_string(text_, key);
        text_ += ':';
    }

    std::string text_;
    bool first_ = true;
};

}  // namespace smz
