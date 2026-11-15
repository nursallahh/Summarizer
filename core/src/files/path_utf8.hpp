#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace smz {

// The C API speaks UTF-8 everywhere. std::filesystem::path uses UTF-16 on Windows, and
// constructing it from a plain std::string there would use the system code page and break
// Turkish file names such as "Ödevler\Çizim şema.pdf". These helpers convert explicitly.

inline std::filesystem::path path_from_utf8(std::string_view utf8) {
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

inline std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

}  // namespace smz
