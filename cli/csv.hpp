#pragma once

// CSV helpers for the label table that people open and edit in Excel.
// - Written as UTF-8 with a byte-order mark, so Excel shows Turkish characters correctly.
// - The separator follows the Windows "list separator" setting (',' or ';'), so Excel splits
//   the columns no matter which regional settings the user has.
// - Reading accepts ',', ';' or tab, detected from the header line.

#include <string>
#include <string_view>
#include <vector>

namespace cli {

using CsvRow = std::vector<std::string>;

// The separator Excel expects on this computer.
char preferred_csv_separator();

std::string csv_escape(std::string_view field, char separator);

// Parses a whole CSV document. Handles quoted fields with separators, quotes and line breaks.
// Removes a leading UTF-8 byte-order mark. Detects the separator from the first line.
std::vector<CsvRow> parse_csv(std::string_view text);

// Excel's plain "CSV" format saves in the old Windows code page (e.g. Windows-1254 for Turkish).
// If `bytes` is not valid UTF-8, converts it from that code page on Windows. Elsewhere returns as is.
std::string to_utf8_if_needed(std::string bytes);

bool is_valid_utf8(std::string_view s);

}  // namespace cli
