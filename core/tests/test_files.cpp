// Tests for file inspection and the import job.
// All test files are generated here at run time (synthetic content only, never real course material).

#include "smz/smz_api.h"

#include "files/file_inspector.hpp"
#include "files/path_utf8.hpp"
#include "files/sha256.hpp"
#include "test_util.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using test::check;
using test::count_occurrences;
namespace fs = std::filesystem;

namespace {

std::string sha256_of(const std::string& data) {
    smz::Sha256 hash;
    hash.update(data.data(), data.size());
    return hash.hex_digest();
}

void write_file(const fs::path& path, const std::string& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

void put_u16(std::string& s, std::uint16_t v, bool little) {
    if (little) {
        s += static_cast<char>(v & 0xFF);
        s += static_cast<char>(v >> 8);
    } else {
        s += static_cast<char>(v >> 8);
        s += static_cast<char>(v & 0xFF);
    }
}

void put_u32(std::string& s, std::uint32_t v, bool little) {
    for (int i = 0; i < 4; ++i) {
        const int shift = little ? 8 * i : 24 - 8 * i;
        s += static_cast<char>((v >> shift) & 0xFF);
    }
}

// A minimal JPEG whose EXIF block has DateTime in IFD0 and DateTimeOriginal in the Exif IFD.
std::string make_jpeg_with_exif(bool little, const std::string& original, const std::string& modified) {
    std::string tiff = little ? "II" : "MM";
    put_u16(tiff, 42, little);
    put_u32(tiff, 8, little);  // IFD0 at offset 8

    // IFD0: 2 entries (DateTime, ExifIFD pointer), then next-IFD = 0.
    const std::uint32_t ifd0 = 8;
    const std::uint32_t ifd0_size = 2 + 2 * 12 + 4;
    const std::uint32_t exif_ifd = ifd0 + ifd0_size;
    const std::uint32_t exif_ifd_size = 2 + 1 * 12 + 4;
    const std::uint32_t modified_at = exif_ifd + exif_ifd_size;
    const std::uint32_t original_at = modified_at + 20;

    put_u16(tiff, 2, little);
    put_u16(tiff, 0x0132, little); put_u16(tiff, 2, little); put_u32(tiff, 20, little); put_u32(tiff, modified_at, little);
    put_u16(tiff, 0x8769, little); put_u16(tiff, 4, little); put_u32(tiff, 1, little); put_u32(tiff, exif_ifd, little);
    put_u32(tiff, 0, little);

    put_u16(tiff, 1, little);
    put_u16(tiff, 0x9003, little); put_u16(tiff, 2, little); put_u32(tiff, 20, little); put_u32(tiff, original_at, little);
    put_u32(tiff, 0, little);

    tiff += modified;
    tiff += '\0';
    tiff += original;
    tiff += '\0';

    std::string jpeg = "\xFF\xD8";
    jpeg += "\xFF\xE1";
    put_u16(jpeg, static_cast<std::uint16_t>(2 + 6 + tiff.size()), false);  // segment length is big-endian
    jpeg += std::string("Exif\0\0", 6);
    jpeg += tiff;
    jpeg += "\xFF\xDA";  // start of scan: metadata ends here
    jpeg += std::string(100, '\x55');
    jpeg += "\xFF\xD9";
    return jpeg;
}

// A zip with only a central directory listing the given names (enough for type detection).
std::string make_zip(const std::vector<std::string>& names) {
    std::string zip = "PK\x03\x04";
    zip += std::string(26, '\0');  // dummy local header body
    const auto dir_offset = static_cast<std::uint32_t>(zip.size());
    std::string dir;
    for (const std::string& name : names) {
        dir += "PK\x01\x02";
        dir += std::string(24, '\0');                                  // fields up to the name length
        put_u16(dir, static_cast<std::uint16_t>(name.size()), true);  // offset 28: name length
        put_u16(dir, 0, true);                                         // 30: extra length
        put_u16(dir, 0, true);                                         // 32: comment length
        dir += std::string(12, '\0');                                  // 34..45
        dir += name;
    }
    zip += dir;
    zip += "PK\x05\x06";
    zip += std::string(4, '\0');
    put_u16(zip, static_cast<std::uint16_t>(names.size()), true);
    put_u16(zip, static_cast<std::uint16_t>(names.size()), true);
    put_u32(zip, static_cast<std::uint32_t>(dir.size()), true);
    put_u32(zip, dir_offset, true);
    put_u16(zip, 0, true);
    return zip;
}

void test_sha256() {
    check(sha256_of("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of empty input");
    check(sha256_of("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of \"abc\"");
    check(sha256_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256 of the 448-bit test vector");
    check(sha256_of(std::string(1'000'000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256 of one million 'a'");

    // Feeding data in odd-sized pieces must give the same digest.
    const std::string data(10'007, 'x');
    smz::Sha256 pieces;
    for (std::size_t i = 0; i < data.size(); i += 63) {
        pieces.update(data.data() + i, std::min<std::size_t>(63, data.size() - i));
    }
    check(pieces.hex_digest() == sha256_of(data), "SHA-256 is the same when fed in pieces");
}

void test_detect_type() {
    check(smz::detect_type("\xFF\xD8\xFF\xE0rest", "jpg", false, false) == "jpeg", "JPEG detected from content");
    check(smz::detect_type("\xFF\xD8\xFF\xE0rest", "pdf", false, false) == "jpeg", "wrong extension does not fool detection");
    check(smz::detect_type("\x89PNG\r\n\x1A\nxxxx", "png", false, false) == "png", "PNG detected");
    check(smz::detect_type("%PDF-1.7\n", "pdf", false, false) == "pdf", "PDF detected");
    check(smz::detect_type(std::string("\0\0\0\x18" "ftypheic", 12), "heic", false, false) == "heic", "HEIC detected");
    check(smz::detect_type(std::string("RIFF\0\0\0\0WEBPVP8 ", 16), "webp", false, false) == "webp", "WebP detected");
    check(smz::detect_type("ID3\x04", "mp3", false, false) == "audio", "MP3 detected as audio");
    check(smz::detect_type("PK\x03\x04", "pptx", true, false) == "pptx", "PowerPoint detected from zip contents");
    check(smz::detect_type("PK\x03\x04", "docx", false, true) == "docx", "Word detected from zip contents");
    check(smz::detect_type("PK\x03\x04", "zip", false, false) == "unsupported", "other zip files are unsupported");
    check(smz::detect_type("Hocanın notu: çalışın", "txt", false, false) == "text", "UTF-8 text note detected");
    check(smz::detect_type("\xC3\x28 broken", "txt", false, false) == "unsupported", "invalid UTF-8 text is rejected");
    check(smz::detect_type("plain", "exe", false, false) == "unsupported", "unknown extension is unsupported");
}

void test_exif() {
    check(smz::read_jpeg_capture_time(make_jpeg_with_exif(true, "2026:10:14 10:32:05", "2026:11:01 09:00:00")) ==
              "2026-10-14T10:32:05",
          "EXIF DateTimeOriginal is preferred (little-endian)");
    check(smz::read_jpeg_capture_time(make_jpeg_with_exif(false, "2026:09:30 14:05:59", "2026:11:01 09:00:00")) ==
              "2026-09-30T14:05:59",
          "EXIF DateTimeOriginal is read (big-endian)");
    check(smz::read_jpeg_capture_time(make_jpeg_with_exif(true, "0000:00:00 00:00:00", "2026:11:01 09:00:00")) ==
              "2026-11-01T09:00:00",
          "zero DateTimeOriginal falls back to DateTime");
    check(smz::read_jpeg_capture_time("\xFF\xD8\xFF\xDA" + std::string(50, '\0')).empty(), "JPEG without EXIF has no time");
    check(smz::read_jpeg_capture_time("not a jpeg").empty(), "non-JPEG has no time");

    // Truncated or corrupt EXIF must not crash or read out of bounds.
    std::string broken = make_jpeg_with_exif(true, "2026:10:14 10:32:05", "2026:11:01 09:00:00");
    for (std::size_t cut = 0; cut < broken.size(); cut += 7) {
        (void)smz::read_jpeg_capture_time(broken.substr(0, cut));
    }
    check(true, "truncated EXIF never crashes");
}

void test_inspect_and_import(const fs::path& root) {
    const fs::path notes = root / smz::path_from_utf8("Ödevler");
    write_file(notes / smz::path_from_utf8("Çizim şema.jpg"),
               make_jpeg_with_exif(true, "2026:10:14 10:32:05", "2026:10:14 10:32:05"));
    write_file(notes / smz::path_from_utf8("hafta3 ders.pptx"),
               make_zip({"[Content_Types].xml", "ppt/presentation.xml", "ppt/slides/slide1.xml",
                         "ppt/slides/slide2.xml", "ppt/slides/slide3.xml", "ppt/slides/_rels/slide1.xml.rels"}));
    write_file(notes / smz::path_from_utf8("not.txt"), "Hoca: final 1-10. haftalar");
    write_file(notes / "alt" / "sinav.pdf", "%PDF-1.4\nfake\n");
    write_file(root / "desktop.ini", "[.ShellClassInfo]");

    const smz::FileInfo jpeg = smz::inspect_file(notes / smz::path_from_utf8("Çizim şema.jpg"));
    check(jpeg.error.empty() && jpeg.type == "jpeg", "inspect: JPEG with a Turkish name");
    check(jpeg.file_name == "Çizim şema.jpg", "inspect: Turkish file name round-trips as UTF-8");
    check(jpeg.captured_at == "2026-10-14T10:32:05", "inspect: capture time from EXIF");
    check(jpeg.sha256.size() == 64, "inspect: SHA-256 is 64 hex characters");

    const smz::FileInfo pptx = smz::inspect_file(notes / smz::path_from_utf8("hafta3 ders.pptx"));
    check(pptx.type == "pptx" && pptx.pages == 3, "inspect: PowerPoint with 3 slides (rels not counted)");

    // An Apple bookmark saved with a .pdf name (seen in real course folders).
    write_file(root / "link" / "1BAA0159.pdf",
               std::string("bplist00\xA3\x01\x02\x03_\x10\x46", 15) +
                   "file:///Ders%205%20belgeler/Ge%C3%A7mi%C5%9F%20s%C4%B1nav.pdf" + std::string(8, '\0'));
    const smz::FileInfo link = smz::inspect_file(root / "link" / "1BAA0159.pdf");
    check(link.type == "unsupported", "Apple link file with a .pdf name is not treated as a PDF");
    check(link.error.find("Apple link file") != std::string::npos, "Apple link file gets a clear explanation");
    check(link.error.find("Ders 5 belgeler/Geçmiş sınav.pdf") != std::string::npos,
          "Apple link file shows its percent-decoded target");

    const smz::FileInfo missing = smz::inspect_file(root / "yok.jpg");
    check(!missing.error.empty() && missing.type == "unsupported", "inspect: missing file reports an error");

    // Import through the public C API: folder, recursive and not, plus one missing path.
    smz_context* ctx = nullptr;
    smz_create(&ctx);
    auto run = [&](const std::string& paths, bool recursive) {
        std::int64_t id = 0;
        smz_import_paths(ctx, paths.c_str(), recursive ? 1 : 0, &id);
        std::string all;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (std::chrono::steady_clock::now() < deadline && all.find("\"type\":\"job_finished\"") == std::string::npos) {
            char* json = nullptr;
            smz_poll_events(ctx, 1000, &json);
            all += json;
            smz_free_string(json);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return all;
    };

    const std::string folder = smz::path_to_utf8(notes);
    const std::string deep = run(folder, true);
    check(count_occurrences(deep, "\"type\":\"file_imported\"") == 4, "import: recursive finds 4 files");
    check(deep.find("\"file_type\":\"pptx\"") != std::string::npos, "import: event carries the detected type");
    check(deep.find("\"pages\":3") != std::string::npos, "import: event carries the slide count");
    check(deep.find("Çizim şema.jpg") != std::string::npos, "import: Turkish names survive the C boundary");
    check(deep.find("\"outcome\":\"succeeded\"") != std::string::npos, "import: job succeeds");

    const std::string shallow = run(folder, false);
    check(count_occurrences(shallow, "\"type\":\"file_imported\"") == 3, "import: non-recursive skips sub-folders");

    const std::string mixed = run(folder + "\n" + smz::path_to_utf8(root / "yok.jpg") + "\r\n", false);
    check(mixed.find("not found:") != std::string::npos, "import: missing paths are reported in the finish message");

    std::int64_t id = 0;
    check(smz_import_paths(ctx, nullptr, 0, &id) == SMZ_ERR_INVALID_ARGUMENT, "import: NULL paths are rejected");
    smz_destroy(ctx);
}

}  // namespace

int main() {
    test_sha256();
    test_detect_type();
    test_exif();

    const fs::path root = fs::temp_directory_path() / ("smz_files_test_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    test_inspect_and_import(root);
    std::error_code ec;
    fs::remove_all(root, ec);

    return test::finish();
}
