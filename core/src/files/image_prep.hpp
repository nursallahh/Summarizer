#pragma once

// Prepares photos for a model: applies the camera's rotation (EXIF orientation) and scales the
// image down so the longer side is at most `max_side` pixels, re-encoded as JPEG.
//
// A 12-megapixel phone photo becomes about 200-400 KB at 1600 px, which is still enough to read
// handwriting and board writing, makes requests much smaller and the local model much faster.
// Uses the Windows Imaging Component (part of Windows). Elsewhere, or if decoding fails, returns
// nullopt and the caller sends the original file.

#include <filesystem>
#include <optional>
#include <string>

namespace smz {

struct PreparedImage {
    std::string jpeg;
    int width = 0;
    int height = 0;
};

std::optional<PreparedImage> prepare_image(const std::filesystem::path& path, int max_side, int jpeg_quality);

}  // namespace smz
