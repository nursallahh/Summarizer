#include "csv.hpp"

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#endif

#include <cstdint>
#include <cwchar>

namespace cli {

char preferred_csv_separator() {
#if defined(_WIN32)
    wchar_t buffer[8] = {};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SLIST, buffer, 8) > 0 &&
        (buffer[0] == L';' || buffer[0] == L',' || buffer[0] == L'\t')) {
        return static_cast<char>(buffer[0]);
    }
#endif
    return ',';
}

std::string csv_escape(std::string_view field, char separator) {
    const bool needs_quotes = field.find_first_of(std::string{separator, '"', '\n', '\r'}) != std::string_view::npos;
    if (!needs_quotes) {
        return std::string(field);
    }
    std::string out = "\"";
    for (const char c : field) {
        if (c == '"') {
            out += '"';
        }
        out += c;
    }
    out += '"';
    return out;
}

std::vector<CsvRow> parse_csv(std::string_view text) {
    if (text.substr(0, 3) == "\xEF\xBB\xBF") {
        text.remove_prefix(3);
    }

    // Detect the separator from the header line (outside quotes).
    char separator = ',';
    {
        int counts[3] = {0, 0, 0};
        const char candidates[3] = {',', ';', '\t'};
        bool quoted = false;
        for (const char c : text) {
            if (c == '"') {
                quoted = !quoted;
            } else if (!quoted && (c == '\n' || c == '\r')) {
                break;
            } else if (!quoted) {
                for (int i = 0; i < 3; ++i) {
                    counts[i] += c == candidates[i] ? 1 : 0;
                }
            }
        }
        int best = 0;
        for (int i = 1; i < 3; ++i) {
            if (counts[i] > counts[best]) {
                best = i;
            }
        }
        separator = candidates[best];
    }

    std::vector<CsvRow> rows;
    CsvRow row;
    std::string field;
    bool quoted = false;
    bool row_has_content = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < text.size() && text[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                field += c;
            }
            continue;
        }
        if (c == '"') {
            quoted = true;
            row_has_content = true;
        } else if (c == separator) {
            row.push_back(std::move(field));
            field.clear();
            row_has_content = true;
        } else if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                ++i;
            }
            if (row_has_content || !field.empty()) {
                row.push_back(std::move(field));
                rows.push_back(std::move(row));
            }
            row.clear();
            field.clear();
            row_has_content = false;
        } else {
            field += c;
            row_has_content = true;
        }
    }
    if (row_has_content || !field.empty()) {
        row.push_back(std::move(field));
        rows.push_back(std::move(row));
    }
    return rows;
}

bool is_valid_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<std::uint8_t>(s[i]);
        std::size_t extra = 0;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0 && c >= 0xC2) {
            extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            extra = 2;
        } else if ((c & 0xF8) == 0xF0 && c <= 0xF4) {
            extra = 3;
        } else {
            return false;
        }
        if (i + extra >= s.size() && extra > 0) {
            return false;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            if ((static_cast<std::uint8_t>(s[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += extra + 1;
    }
    return true;
}

std::string to_utf8_if_needed(std::string bytes) {
    if (is_valid_utf8(bytes)) {
        return bytes;
    }
#if defined(_WIN32)
    // This process runs with the UTF-8 code page (see summarizer.manifest), so ask Windows for
    // the user's real legacy code page, which is what Excel's plain CSV export uses.
    UINT code_page = 1252;
    wchar_t buffer[8] = {};
    if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE, buffer, 8) > 0) {
        code_page = static_cast<UINT>(std::wcstoul(buffer, nullptr, 10));
    }
    const int wide_len = MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (wide_len <= 0) {
        return bytes;
    }
    std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
    MultiByteToWideChar(code_page, 0, bytes.data(), static_cast<int>(bytes.size()), wide.data(), wide_len);
    const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(utf8_len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), wide_len, utf8.data(), utf8_len, nullptr, nullptr);
    return utf8;
#else
    return bytes;
#endif
}

}  // namespace cli
