#pragma once

// Small JSON reader for the command-line tool: enough to read the core's event batches.
// Supports objects, arrays, strings (with \u escapes and surrogate pairs), numbers, booleans and null.

#include <cctype>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cli {

struct JsonValue {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;

    const JsonValue* find(std::string_view key) const {
        for (const auto& [k, v] : object) {
            if (k == key) {
                return &v;
            }
        }
        return nullptr;
    }

    std::string string_or(std::string_view key, std::string fallback = {}) const {
        const JsonValue* v = find(key);
        return v != nullptr && v->type == Type::String ? v->string : fallback;
    }

    std::int64_t int_or(std::string_view key, std::int64_t fallback = 0) const {
        const JsonValue* v = find(key);
        return v != nullptr && v->type == Type::Number ? static_cast<std::int64_t>(v->number) : fallback;
    }
};

class JsonReader {
public:
    static std::optional<JsonValue> parse(std::string_view text) {
        JsonReader reader(text);
        JsonValue value;
        if (!reader.value(value, 0)) {
            return std::nullopt;
        }
        reader.skip_space();
        if (reader.pos_ != text.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    explicit JsonReader(std::string_view text) : text_(text) {}

    static constexpr int kMaxDepth = 64;

    void skip_space() {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\t' || text_[pos_] == '\n' || text_[pos_] == '\r')) {
            ++pos_;
        }
    }

    bool consume(char c) {
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    bool literal(std::string_view word) {
        if (text_.substr(pos_, word.size()) == word) {
            pos_ += word.size();
            return true;
        }
        return false;
    }

    bool value(JsonValue& out, int depth) {
        if (depth > kMaxDepth) {
            return false;
        }
        skip_space();
        if (pos_ >= text_.size()) {
            return false;
        }
        const char c = text_[pos_];
        if (c == '{') {
            return object(out, depth);
        }
        if (c == '[') {
            return array(out, depth);
        }
        if (c == '"') {
            out.type = JsonValue::Type::String;
            return string(out.string);
        }
        if (literal("true")) {
            out.type = JsonValue::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (literal("false")) {
            out.type = JsonValue::Type::Bool;
            return true;
        }
        if (literal("null")) {
            out.type = JsonValue::Type::Null;
            return true;
        }
        return number(out);
    }

    bool object(JsonValue& out, int depth) {
        out.type = JsonValue::Type::Object;
        ++pos_;  // '{'
        if (consume('}')) {
            return true;
        }
        do {
            skip_space();
            std::string key;
            if (pos_ >= text_.size() || text_[pos_] != '"' || !string(key) || !consume(':')) {
                return false;
            }
            JsonValue v;
            if (!value(v, depth + 1)) {
                return false;
            }
            out.object.emplace_back(std::move(key), std::move(v));
        } while (consume(','));
        return consume('}');
    }

    bool array(JsonValue& out, int depth) {
        out.type = JsonValue::Type::Array;
        ++pos_;  // '['
        if (consume(']')) {
            return true;
        }
        do {
            JsonValue v;
            if (!value(v, depth + 1)) {
                return false;
            }
            out.array.push_back(std::move(v));
        } while (consume(','));
        return consume(']');
    }

    bool number(JsonValue& out) {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && (text_[pos_] == '-' || text_[pos_] == '+')) {
            ++pos_;
        }
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                                       text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '-' ||
                                       text_[pos_] == '+')) {
            ++pos_;
        }
        if (pos_ == start) {
            return false;
        }
        try {
            out.number = std::stod(std::string(text_.substr(start, pos_ - start)));
        } catch (...) {
            return false;
        }
        out.type = JsonValue::Type::Number;
        return true;
    }

    static void append_utf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool hex4(std::uint32_t& out) {
        if (pos_ + 4 > text_.size()) {
            return false;
        }
        out = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            out <<= 4;
            if (c >= '0' && c <= '9') {
                out |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                out |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                out |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
        }
        return true;
    }

    bool string(std::string& out) {
        ++pos_;  // opening quote
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size()) {
                return false;
            }
            const char e = text_[pos_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    std::uint32_t cp = 0;
                    if (!hex4(cp)) {
                        return false;
                    }
                    if (cp >= 0xD800 && cp <= 0xDBFF && text_.substr(pos_, 2) == "\\u") {
                        pos_ += 2;
                        std::uint32_t low = 0;
                        if (!hex4(low) || low < 0xDC00 || low > 0xDFFF) {
                            return false;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    append_utf8(out, cp);
                    break;
                }
                default:
                    return false;
            }
        }
        return false;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

}  // namespace cli
