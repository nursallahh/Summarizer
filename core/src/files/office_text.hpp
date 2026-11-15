#pragma once

// Reads the text of PowerPoint (.pptx) and Word (.docx) files without dependencies:
// a small zip reader and a DEFLATE decoder (RFC 1951), enough for Office files.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace smz {

// Decompresses a raw DEFLATE stream. Returns nullopt on corrupt data or if the output
// would exceed `max_output` bytes.
std::optional<std::string> inflate_raw(std::string_view compressed, std::size_t max_output);

struct OfficeText {
    std::string text;   // UTF-8; slides separated by "\n--- Slide N ---\n"
    int slides = 0;     // pptx only
    std::string error;  // empty on success
};

// Extracts the visible text in reading order: slide text plus speaker notes for pptx,
// paragraphs for docx. Stops after `max_chars` characters.
OfficeText extract_office_text(const std::filesystem::path& path, std::size_t max_chars);

}  // namespace smz
