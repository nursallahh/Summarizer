#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace smz {

// What the core knows about a file right after it is added, before any OCR or AI.
struct FileInfo {
    std::string path;         // UTF-8, as given
    std::string file_name;    // UTF-8, name only
    std::string sha256;       // content id; empty if the file could not be read
    std::string type;         // see detect_type()
    std::int64_t size = 0;    // bytes
    std::int32_t pages = -1;  // slides for presentations; -1 when not known yet (PDF pages arrive with PDFium)
    std::string captured_at;  // photos: EXIF capture time "YYYY-MM-DDTHH:MM:SS" (camera local time); else empty
    std::string error;        // empty on success
};

// Detects the file type from its first bytes, not its extension, plus the extension only for
// plain text. Returns one of:
//   "jpeg", "png", "heic", "webp", "pdf", "pptx", "docx", "text", "audio", "unsupported".
// `zip_has` answers whether a zip archive contains an entry (used to tell pptx from docx).
std::string detect_type(std::string_view head, std::string_view extension_lower,
                        bool zip_has_ppt, bool zip_has_word);

// Reads EXIF DateTimeOriginal (or DateTime) from the start of a JPEG file.
// Returns "YYYY-MM-DDTHH:MM:SS" or an empty string.
std::string read_jpeg_capture_time(std::string_view jpeg_head);

// Reads a whole file once: hash, type, size, slide count and capture time.
// Never throws; problems are reported in FileInfo::error.
FileInfo inspect_file(const std::filesystem::path& path);

}  // namespace smz
