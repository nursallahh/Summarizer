#include "files/file_inspector.hpp"

#include "files/path_utf8.hpp"
#include "files/sha256.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <fstream>
#include <system_error>
#include <vector>

namespace smz {

namespace {

constexpr std::size_t kHeadBytes = 256 * 1024;  // enough for JPEG EXIF and every signature we check
constexpr std::size_t kChunkBytes = 64 * 1024;
constexpr std::size_t kMaxZipDirectory = 16 * 1024 * 1024;

bool starts_with(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

std::uint16_t read_u16(std::string_view d, std::size_t at, bool little) {
    const auto b0 = static_cast<std::uint8_t>(d[at]);
    const auto b1 = static_cast<std::uint8_t>(d[at + 1]);
    return little ? static_cast<std::uint16_t>(b0 | (b1 << 8)) : static_cast<std::uint16_t>((b0 << 8) | b1);
}

std::uint32_t read_u32(std::string_view d, std::size_t at, bool little) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i) {
        const auto b = static_cast<std::uint8_t>(d[at + (little ? 3 - i : i)]);
        v = (v << 8) | b;
    }
    return v;
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
        if (i + extra >= s.size()) {
            // A multi-byte sequence cut off by the end of the sample is fine (we only read the head).
            return true;
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

// "YYYY:MM:DD HH:MM:SS" -> "YYYY-MM-DDTHH:MM:SS"; empty if malformed or zero.
std::string normalize_exif_time(std::string_view raw) {
    if (raw.size() < 19) {
        return {};
    }
    raw = raw.substr(0, 19);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const bool separator = i == 4 || i == 7 || i == 10 || i == 13 || i == 16;
        if (!separator && !std::isdigit(static_cast<unsigned char>(raw[i]))) {
            return {};
        }
    }
    if (raw.substr(0, 4) == "0000") {
        return {};
    }
    std::string out(raw);
    out[4] = '-';
    out[7] = '-';
    out[10] = 'T';
    out[13] = ':';
    out[16] = ':';
    return out;
}

struct TiffReader {
    std::string_view data;
    bool little = true;

    // Returns the ASCII value of `tag` in the IFD at `ifd_offset`, or empty.
    // Also reports the Exif sub-IFD pointer if found.
    std::string find_ascii(std::uint32_t ifd_offset, std::uint16_t tag, std::uint32_t* exif_ifd = nullptr) const {
        if (ifd_offset + 2 > data.size()) {
            return {};
        }
        const std::uint16_t count = read_u16(data, ifd_offset, little);
        std::string result;
        for (std::uint32_t i = 0; i < count; ++i) {
            const std::size_t entry = ifd_offset + 2 + 12 * static_cast<std::size_t>(i);
            if (entry + 12 > data.size()) {
                break;
            }
            const std::uint16_t entry_tag = read_u16(data, entry, little);
            const std::uint16_t type = read_u16(data, entry + 2, little);
            const std::uint32_t n = read_u32(data, entry + 4, little);
            if (entry_tag == 0x8769 && exif_ifd != nullptr) {
                *exif_ifd = read_u32(data, entry + 8, little);
            }
            if (entry_tag == tag && type == 2 /* ASCII */ && n >= 19) {
                const std::uint32_t value_at = n > 4 ? read_u32(data, entry + 8, little) : static_cast<std::uint32_t>(entry + 8);
                if (value_at + 19 <= data.size()) {
                    result = std::string(data.substr(value_at, 19));
                }
            }
        }
        return result;
    }
};

std::string capture_time_from_tiff(std::string_view tiff) {
    if (tiff.size() < 8) {
        return {};
    }
    TiffReader reader{tiff, true};
    if (starts_with(tiff, "II")) {
        reader.little = true;
    } else if (starts_with(tiff, "MM")) {
        reader.little = false;
    } else {
        return {};
    }
    if (read_u16(tiff, 2, reader.little) != 42) {
        return {};
    }
    const std::uint32_t ifd0 = read_u32(tiff, 4, reader.little);
    std::uint32_t exif_ifd = 0;
    const std::string date_time = reader.find_ascii(ifd0, 0x0132, &exif_ifd);  // DateTime (last modified)

    if (exif_ifd != 0) {
        for (const std::uint16_t tag : {std::uint16_t{0x9003}, std::uint16_t{0x9004}}) {  // DateTimeOriginal, Digitized
            const std::string value = normalize_exif_time(reader.find_ascii(exif_ifd, tag));
            if (!value.empty()) {
                return value;
            }
        }
    }
    return normalize_exif_time(date_time);
}

struct ZipFacts {
    bool has_ppt = false;
    bool has_word = false;
    int slides = 0;
};

// Reads only the zip central directory (file names), never decompresses anything.
ZipFacts read_zip_directory(const std::filesystem::path& path, std::uint64_t file_size) {
    ZipFacts facts;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return facts;
    }

    const std::uint64_t tail_size = std::min<std::uint64_t>(file_size, 65'557);
    std::string tail(static_cast<std::size_t>(tail_size), '\0');
    in.seekg(static_cast<std::streamoff>(file_size - tail_size));
    in.read(tail.data(), static_cast<std::streamsize>(tail.size()));
    if (!in) {
        return facts;
    }

    // End of central directory record: signature 0x06054b50, searched from the end.
    if (tail.size() < 22) {
        return facts;
    }
    std::size_t eocd = std::string::npos;
    for (std::size_t i = tail.size() - 22 + 1; i-- > 0;) {
        if (tail.compare(i, 4, "PK\x05\x06", 4) == 0) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) {
        return facts;
    }
    const std::uint32_t dir_size = read_u32(tail, eocd + 12, true);
    const std::uint32_t dir_offset = read_u32(tail, eocd + 16, true);
    if (dir_size > kMaxZipDirectory || std::uint64_t{dir_offset} + dir_size > file_size) {
        return facts;
    }

    std::string dir(dir_size, '\0');
    in.seekg(dir_offset);
    in.read(dir.data(), dir_size);
    if (!in) {
        return facts;
    }

    std::size_t pos = 0;
    while (pos + 46 <= dir.size() && dir.compare(pos, 4, "PK\x01\x02", 4) == 0) {
        const std::uint16_t name_len = read_u16(dir, pos + 28, true);
        const std::uint16_t extra_len = read_u16(dir, pos + 30, true);
        const std::uint16_t comment_len = read_u16(dir, pos + 32, true);
        if (pos + 46 + name_len > dir.size()) {
            break;
        }
        const std::string_view name(dir.data() + pos + 46, name_len);
        if (name == "ppt/presentation.xml") {
            facts.has_ppt = true;
        } else if (name == "word/document.xml") {
            facts.has_word = true;
        }
        constexpr std::string_view kSlidePrefix = "ppt/slides/slide";
        if (starts_with(name, kSlidePrefix) && name.size() > kSlidePrefix.size() + 4 &&
            name.substr(name.size() - 4) == ".xml" &&
            name.find('/', kSlidePrefix.size()) == std::string_view::npos) {
            ++facts.slides;
        }
        pos += 46 + std::size_t{name_len} + extra_len + comment_len;
    }
    return facts;
}

std::string lower_extension(const std::filesystem::path& path) {
    std::string ext = path_to_utf8(path.extension());
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (!ext.empty() && ext[0] == '.') {
        ext.erase(0, 1);
    }
    return ext;
}


// Apple devices sometimes save a small "bookmark" (binary property list) instead of the document
// itself, e.g. when sharing an iCloud file. It often has the document's extension, so it looks like
// a PDF but is not. Returns the most likely target path stored in it, percent-decoded, or "".
std::string apple_link_target(std::string_view head) {
    std::string_view best;
    std::size_t start = 0;
    for (std::size_t i = 0; i <= head.size(); ++i) {
        const bool printable = i < head.size() && static_cast<unsigned char>(head[i]) >= 0x20 &&
                               static_cast<unsigned char>(head[i]) < 0x7F;
        if (!printable) {
            const std::string_view run = head.substr(start, i - start);
            if (run.size() > best.size() && run.find('/') != std::string_view::npos) {
                best = run;
            }
            start = i + 1;
        }
    }
    std::string decoded;
    for (std::size_t i = 0; i < best.size(); ++i) {
        if (best[i] == '%' && i + 2 < best.size() && std::isxdigit(static_cast<unsigned char>(best[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(best[i + 2]))) {
            decoded += static_cast<char>(std::stoi(std::string(best.substr(i + 1, 2)), nullptr, 16));
            i += 2;
        } else {
            decoded += best[i];
        }
    }
    if (!is_valid_utf8(decoded)) {
        decoded = std::string(best);
    }
    if (decoded.size() > 160) {
        decoded = "..." + decoded.substr(decoded.size() - 157);
    }
    return decoded;
}

}  // namespace

std::string detect_type(std::string_view head, std::string_view extension_lower, bool zip_has_ppt, bool zip_has_word) {
    if (starts_with(head, "\xFF\xD8\xFF")) {
        return "jpeg";
    }
    if (starts_with(head, "\x89PNG\r\n\x1A\n")) {
        return "png";
    }
    if (head.substr(0, std::min<std::size_t>(head.size(), 1024)).find("%PDF-") != std::string_view::npos) {
        return "pdf";
    }
    if (head.size() >= 12 && head.substr(4, 4) == "ftyp") {
        const std::string_view brand = head.substr(8, 4);
        if (brand == "heic" || brand == "heix" || brand == "hevc" || brand == "hevx" || brand == "mif1" ||
            brand == "msf1") {
            return "heic";
        }
        if (brand == "M4A " || brand == "M4B ") {
            return "audio";
        }
    }
    if (head.size() >= 12 && starts_with(head, "RIFF")) {
        if (head.substr(8, 4) == "WEBP") {
            return "webp";
        }
        if (head.substr(8, 4) == "WAVE") {
            return "audio";
        }
    }
    if (starts_with(head, "ID3") || starts_with(head, "OggS") ||
        (head.size() >= 2 && static_cast<std::uint8_t>(head[0]) == 0xFF &&
         (static_cast<std::uint8_t>(head[1]) & 0xE0) == 0xE0)) {
        return "audio";
    }
    if (starts_with(head, "PK\x03\x04")) {
        if (zip_has_ppt) {
            return "pptx";
        }
        if (zip_has_word) {
            return "docx";
        }
        return "unsupported";
    }
    if ((extension_lower == "txt" || extension_lower == "md") && is_valid_utf8(head)) {
        return "text";
    }
    return "unsupported";
}

std::string read_jpeg_capture_time(std::string_view jpeg) {
    if (!starts_with(jpeg, "\xFF\xD8")) {
        return {};
    }
    std::size_t pos = 2;
    while (pos + 4 <= jpeg.size()) {
        if (static_cast<std::uint8_t>(jpeg[pos]) != 0xFF) {
            return {};
        }
        const auto marker = static_cast<std::uint8_t>(jpeg[pos + 1]);
        if (marker == 0xD9 || marker == 0xDA) {  // end of image / start of scan: no more metadata
            return {};
        }
        const std::uint16_t length = read_u16(jpeg, pos + 2, false);
        if (length < 2 || pos + 2 + length > jpeg.size()) {
            return {};
        }
        if (marker == 0xE1 && length >= 8 && jpeg.compare(pos + 4, 6, std::string_view("Exif\0\0", 6)) == 0) {
            return capture_time_from_tiff(jpeg.substr(pos + 10, length - 8));
        }
        pos += 2 + std::size_t{length};
    }
    return {};
}

FileInfo inspect_file(const std::filesystem::path& path) {
    FileInfo info;
    info.path = path_to_utf8(path);
    info.file_name = path_to_utf8(path.filename());

    try {
        std::error_code ec;
        const std::uint64_t size = std::filesystem::file_size(path, ec);
        if (ec) {
            info.error = "cannot read size: " + ec.message();
            info.type = "unsupported";
            return info;
        }
        info.size = static_cast<std::int64_t>(size);

        std::ifstream in(path, std::ios::binary);
        if (!in) {
            info.error = "cannot open file";
            info.type = "unsupported";
            return info;
        }

        Sha256 hash;
        std::string head;
        head.reserve(std::min<std::uint64_t>(size, kHeadBytes));
        std::vector<char> chunk(kChunkBytes);
        while (in) {
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto got = static_cast<std::size_t>(in.gcount());
            if (got == 0) {
                break;
            }
            hash.update(chunk.data(), got);
            if (head.size() < kHeadBytes) {
                head.append(chunk.data(), std::min(got, kHeadBytes - head.size()));
            }
        }
        if (in.bad()) {
            info.error = "read error";
            info.type = "unsupported";
            return info;
        }
        info.sha256 = hash.hex_digest();

        ZipFacts zip;
        if (starts_with(head, "PK\x03\x04")) {
            zip = read_zip_directory(path, size);
        }
        info.type = detect_type(head, lower_extension(path), zip.has_ppt, zip.has_word);
        if (starts_with(head, "bplist00")) {
            const std::string target = apple_link_target(head);
            info.error = "Apple link file, not the document itself" +
                         (target.empty() ? std::string() : " (points to: " + target + ")") +
                         ". Copy the real file from the iPhone, iPad, Mac or iCloud.";
        }
        if (info.type == "pptx") {
            info.pages = zip.slides;
        }
        if (info.type == "jpeg") {
            info.captured_at = read_jpeg_capture_time(head);
        }
    } catch (const std::exception& e) {
        info.error = e.what();
        if (info.type.empty()) {
            info.type = "unsupported";
        }
    }
    return info;
}

}  // namespace smz
