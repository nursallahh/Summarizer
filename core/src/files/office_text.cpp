#include "files/office_text.hpp"

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <vector>

namespace smz {

namespace {

// ---- DEFLATE (RFC 1951) ----

class BitReader {
public:
    explicit BitReader(std::string_view data) : data_(data) {}

    // Returns -1 past the end of input.
    int bit() {
        if (pos_ >= data_.size()) {
            return -1;
        }
        const int b = (static_cast<unsigned char>(data_[pos_]) >> bit_) & 1;
        if (++bit_ == 8) {
            bit_ = 0;
            ++pos_;
        }
        return b;
    }

    // Reads `count` bits, least significant first. Returns -1 past the end.
    long bits(int count) {
        long value = 0;
        for (int i = 0; i < count; ++i) {
            const int b = bit();
            if (b < 0) {
                return -1;
            }
            value |= static_cast<long>(b) << i;
        }
        return value;
    }

    void align_to_byte() {
        if (bit_ != 0) {
            bit_ = 0;
            ++pos_;
        }
    }

    bool read_bytes(std::size_t n, std::string& out) {
        if (pos_ + n > data_.size()) {
            return false;
        }
        out.append(data_.substr(pos_, n));
        pos_ += n;
        return true;
    }

private:
    std::string_view data_;
    std::size_t pos_ = 0;
    int bit_ = 0;
};

// Canonical Huffman table as in RFC 1951 3.2.2: counts per length and symbols sorted by code.
struct Huffman {
    std::array<std::uint16_t, 16> count{};
    std::vector<std::uint16_t> symbol;

    bool build(const std::uint8_t* lengths, int n) {
        count.fill(0);
        for (int i = 0; i < n; ++i) {
            ++count[lengths[i]];
        }
        count[0] = 0;
        int left = 1;
        for (int len = 1; len < 16; ++len) {
            left <<= 1;
            left -= count[len];
            if (left < 0) {
                return false;  // over-subscribed
            }
        }
        std::array<std::uint16_t, 16> offset{};
        for (int len = 1; len < 15; ++len) {
            offset[len + 1] = static_cast<std::uint16_t>(offset[len] + count[len]);
        }
        symbol.assign(static_cast<std::size_t>(n), 0);
        for (int i = 0; i < n; ++i) {
            if (lengths[i] != 0) {
                symbol[offset[lengths[i]]++] = static_cast<std::uint16_t>(i);
            }
        }
        return true;
    }

    // Returns the decoded symbol, or -1 on error.
    int decode(BitReader& in) const {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len < 16; ++len) {
            const int b = in.bit();
            if (b < 0) {
                return -1;
            }
            code |= b;
            const int c = count[static_cast<std::size_t>(len)];
            if (code - c < first) {
                return symbol[static_cast<std::size_t>(index + (code - first))];
            }
            index += c;
            first += c;
            first <<= 1;
            code <<= 1;
        }
        return -1;
    }
};

constexpr std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                           31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
constexpr std::uint8_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                           2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
constexpr std::uint16_t kDistBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                         33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
constexpr std::uint8_t kDistExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                         6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

bool inflate_block(BitReader& in, std::string& out, const Huffman& lit, const Huffman& dist, std::size_t max_output) {
    for (;;) {
        const int sym = lit.decode(in);
        if (sym < 0) {
            return false;
        }
        if (sym < 256) {
            out += static_cast<char>(sym);
        } else if (sym == 256) {
            return true;
        } else {
            const int li = sym - 257;
            if (li >= 29) {
                return false;
            }
            const long extra = in.bits(kLengthExtra[li]);
            const int ds = dist.decode(in);
            if (extra < 0 || ds < 0 || ds >= 30) {
                return false;
            }
            const long dextra = in.bits(kDistExtra[ds]);
            if (dextra < 0) {
                return false;
            }
            const std::size_t length = kLengthBase[li] + static_cast<std::size_t>(extra);
            const std::size_t distance = kDistBase[ds] + static_cast<std::size_t>(dextra);
            if (distance > out.size()) {
                return false;
            }
            const std::size_t from = out.size() - distance;
            for (std::size_t i = 0; i < length; ++i) {
                out += out[from + i];  // byte by byte: copies may overlap
            }
        }
        if (out.size() > max_output) {
            return false;
        }
    }
}

bool read_dynamic_tables(BitReader& in, Huffman& lit, Huffman& dist) {
    const long hlit = in.bits(5);
    const long hdist = in.bits(5);
    const long hclen = in.bits(4);
    if (hlit < 0 || hdist < 0 || hclen < 0) {
        return false;
    }
    const int nlit = static_cast<int>(hlit) + 257;
    const int ndist = static_cast<int>(hdist) + 1;
    const int ncode = static_cast<int>(hclen) + 4;
    if (nlit > 286 || ndist > 30) {
        return false;
    }
    static constexpr std::uint8_t kOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    std::uint8_t code_lengths[19] = {};
    for (int i = 0; i < ncode; ++i) {
        const long v = in.bits(3);
        if (v < 0) {
            return false;
        }
        code_lengths[kOrder[i]] = static_cast<std::uint8_t>(v);
    }
    Huffman code;
    if (!code.build(code_lengths, 19)) {
        return false;
    }
    std::uint8_t lengths[286 + 30] = {};
    int index = 0;
    while (index < nlit + ndist) {
        const int sym = code.decode(in);
        if (sym < 0) {
            return false;
        }
        if (sym < 16) {
            lengths[index++] = static_cast<std::uint8_t>(sym);
            continue;
        }
        std::uint8_t value = 0;
        long repeat = 0;
        if (sym == 16) {
            if (index == 0) {
                return false;
            }
            value = lengths[index - 1];
            repeat = in.bits(2);
            repeat = repeat < 0 ? -1 : 3 + repeat;
        } else if (sym == 17) {
            repeat = in.bits(3);
            repeat = repeat < 0 ? -1 : 3 + repeat;
        } else {
            repeat = in.bits(7);
            repeat = repeat < 0 ? -1 : 11 + repeat;
        }
        if (repeat < 0 || index + repeat > nlit + ndist) {
            return false;
        }
        while (repeat-- > 0) {
            lengths[index++] = value;
        }
    }
    if (lengths[256] == 0) {
        return false;  // no end-of-block code
    }
    return lit.build(lengths, nlit) && dist.build(lengths + nlit, ndist);
}

// ---- zip ----

std::uint32_t u16(std::string_view d, std::size_t at) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(d[at])) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(d[at + 1])) << 8;
}

std::uint32_t u32(std::string_view d, std::size_t at) {
    return u16(d, at) | u16(d, at + 2) << 16;
}

struct ZipEntry {
    std::uint32_t method = 0;
    std::uint32_t compressed = 0;
    std::uint32_t uncompressed = 0;
    std::uint32_t local_offset = 0;
};

class ZipFile {
public:
    bool open(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        data_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        const std::string_view d = data_;
        if (d.size() < 22) {
            return false;
        }
        std::size_t eocd = std::string::npos;
        for (std::size_t i = d.size() - 22 + 1; i-- > 0 && d.size() - i <= 22 + 65535;) {
            if (u32(d, i) == 0x06054b50) {
                eocd = i;
                break;
            }
        }
        if (eocd == std::string::npos) {
            return false;
        }
        const std::uint32_t entries = u16(d, eocd + 10);
        std::size_t at = u32(d, eocd + 16);
        for (std::uint32_t e = 0; e < entries; ++e) {
            if (at + 46 > d.size() || u32(d, at) != 0x02014b50) {
                return false;
            }
            const std::uint32_t name_len = u16(d, at + 28);
            const std::uint32_t extra_len = u16(d, at + 30);
            const std::uint32_t comment_len = u16(d, at + 32);
            if (at + 46 + name_len > d.size()) {
                return false;
            }
            ZipEntry entry{u16(d, at + 10), u32(d, at + 20), u32(d, at + 24), u32(d, at + 42)};
            entries_[std::string(d.substr(at + 46, name_len))] = entry;
            at += 46 + name_len + extra_len + comment_len;
        }
        return true;
    }

    bool has(const std::string& name) const { return entries_.count(name) != 0; }

    const std::map<std::string, ZipEntry>& entries() const { return entries_; }

    std::optional<std::string> read(const std::string& name) const {
        const auto it = entries_.find(name);
        if (it == entries_.end()) {
            return std::nullopt;
        }
        const std::string_view d = data_;
        const ZipEntry& e = it->second;
        if (static_cast<std::size_t>(e.local_offset) + 30 > d.size() || u32(d, e.local_offset) != 0x04034b50) {
            return std::nullopt;
        }
        const std::size_t start = e.local_offset + 30 + u16(d, e.local_offset + 26) + u16(d, e.local_offset + 28);
        if (start + e.compressed > d.size()) {
            return std::nullopt;
        }
        const std::string_view body = d.substr(start, e.compressed);
        constexpr std::size_t kMaxEntry = 64u * 1024u * 1024u;
        if (e.method == 0) {
            return std::string(body);
        }
        if (e.method == 8) {
            return inflate_raw(body, kMaxEntry);
        }
        return std::nullopt;
    }

private:
    std::string data_;
    std::map<std::string, ZipEntry> entries_;
};

// ---- XML text ----

void append_decoded_entity(std::string_view entity, std::string& out) {
    if (entity == "amp") {
        out += '&';
    } else if (entity == "lt") {
        out += '<';
    } else if (entity == "gt") {
        out += '>';
    } else if (entity == "quot") {
        out += '"';
    } else if (entity == "apos") {
        out += '\'';
    } else if (entity.size() > 1 && entity[0] == '#') {
        unsigned long cp = 0;
        try {
            cp = entity[1] == 'x' || entity[1] == 'X' ? std::stoul(std::string(entity.substr(2)), nullptr, 16)
                                                      : std::stoul(std::string(entity.substr(1)), nullptr, 10);
        } catch (...) {
            return;
        }
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x110000) {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
}

// Collects the contents of <prefix:t> elements; a line break after each </prefix:p>.
std::string xml_text(std::string_view xml, std::string_view prefix) {
    const std::string open_t = "<" + std::string(prefix) + ":t";
    const std::string close_t = "</" + std::string(prefix) + ":t>";
    const std::string close_p = "</" + std::string(prefix) + ":p>";
    std::string out;
    std::size_t i = 0;
    while (i < xml.size()) {
        const std::size_t t = xml.find(open_t, i);
        const std::size_t p = xml.find(close_p, i);
        if (p != std::string_view::npos && (t == std::string_view::npos || p < t)) {
            if (!out.empty() && out.back() != '\n') {
                out += '\n';
            }
            i = p + close_p.size();
            continue;
        }
        if (t == std::string_view::npos) {
            break;
        }
        // "<a:t>" or "<a:t xml:space=...>", but not "<a:tab" etc.
        const char after = t + open_t.size() < xml.size() ? xml[t + open_t.size()] : '\0';
        if (after != '>' && after != ' ') {
            i = t + open_t.size();
            continue;
        }
        const std::size_t start = xml.find('>', t);
        const std::size_t end = xml.find(close_t, start);
        if (start == std::string_view::npos || end == std::string_view::npos) {
            break;
        }
        const std::string_view raw = xml.substr(start + 1, end - start - 1);
        for (std::size_t k = 0; k < raw.size(); ++k) {
            if (raw[k] == '&') {
                const std::size_t semi = raw.find(';', k);
                if (semi != std::string_view::npos && semi - k <= 10) {
                    append_decoded_entity(raw.substr(k + 1, semi - k - 1), out);
                    k = semi;
                    continue;
                }
            }
            out += raw[k];
        }
        i = end + close_t.size();
    }
    return out;
}

// "ppt/slides/slide12.xml" -> 12, otherwise -1.
int numbered_part(const std::string& name, std::string_view prefix) {
    if (name.rfind(prefix, 0) != 0 || name.size() < prefix.size() + 5 || name.substr(name.size() - 4) != ".xml") {
        return -1;
    }
    const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - 4);
    if (digits.empty() || !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        return -1;
    }
    return std::stoi(digits);
}

}  // namespace

std::optional<std::string> inflate_raw(std::string_view compressed, std::size_t max_output) {
    BitReader in(compressed);
    std::string out;
    for (;;) {
        const int final_block = in.bit();
        const long type = in.bits(2);
        if (final_block < 0 || type < 0) {
            return std::nullopt;
        }
        if (type == 0) {
            in.align_to_byte();
            std::string header;
            if (!in.read_bytes(4, header)) {
                return std::nullopt;
            }
            const std::uint32_t len = u16(header, 0);
            const std::uint32_t nlen = u16(header, 2);
            if ((len ^ 0xFFFF) != nlen || !in.read_bytes(len, out)) {
                return std::nullopt;
            }
        } else if (type == 1) {
            static const std::pair<Huffman, Huffman> fixed = [] {
                std::uint8_t l[288];
                std::fill(l, l + 144, 8);
                std::fill(l + 144, l + 256, 9);
                std::fill(l + 256, l + 280, 7);
                std::fill(l + 280, l + 288, 8);
                std::uint8_t d[30];
                std::fill(d, d + 30, 5);
                std::pair<Huffman, Huffman> t;
                t.first.build(l, 288);
                t.second.build(d, 30);
                return t;
            }();
            if (!inflate_block(in, out, fixed.first, fixed.second, max_output)) {
                return std::nullopt;
            }
        } else if (type == 2) {
            Huffman lit;
            Huffman dist;
            if (!read_dynamic_tables(in, lit, dist) || !inflate_block(in, out, lit, dist, max_output)) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
        if (out.size() > max_output) {
            return std::nullopt;
        }
        if (final_block == 1) {
            return out;
        }
    }
}

OfficeText extract_office_text(const std::filesystem::path& path, std::size_t max_chars) {
    OfficeText result;
    ZipFile zip;
    if (!zip.open(path)) {
        result.error = "not a readable zip file";
        return result;
    }

    if (zip.has("word/document.xml")) {
        const auto xml = zip.read("word/document.xml");
        if (!xml) {
            result.error = "could not decompress word/document.xml";
            return result;
        }
        result.text = xml_text(*xml, "w");
    } else {
        std::map<int, std::string> slides;
        for (const auto& [name, entry] : zip.entries()) {
            const int n = numbered_part(name, "ppt/slides/slide");
            if (n >= 0) {
                slides[n] = name;
            }
        }
        if (slides.empty()) {
            result.error = "no slides or document text found";
            return result;
        }
        for (const auto& [n, name] : slides) {
            ++result.slides;
            result.text += "\n--- Slide " + std::to_string(n) + " ---\n";
            if (const auto xml = zip.read(name)) {
                result.text += xml_text(*xml, "a");
            }
            // Speaker notes: the slide's relationships name its notes part.
            const std::string rels_name = "ppt/slides/_rels/slide" + std::to_string(n) + ".xml.rels";
            if (const auto rels = zip.read(rels_name)) {
                const std::size_t at = rels->find("../notesSlides/");
                if (at != std::string::npos) {
                    const std::size_t end = rels->find('"', at);
                    const std::string notes_name = "ppt/notesSlides/" + rels->substr(at + 15, end - at - 15);
                    if (const auto notes = zip.read(notes_name)) {
                        const std::string notes_text = xml_text(*notes, "a");
                        // Notes parts also hold the slide number placeholder; skip bare numbers.
                        if (notes_text.find_first_not_of("0123456789\n ") != std::string::npos) {
                            result.text += "[Notes] " + notes_text;
                        }
                    }
                }
            }
            if (result.text.size() >= max_chars) {
                break;
            }
        }
    }
    if (result.text.size() > max_chars) {
        result.text.resize(max_chars);
        // Do not cut a UTF-8 sequence in half.
        while (!result.text.empty() && (static_cast<unsigned char>(result.text.back()) & 0xC0) == 0x80) {
            result.text.pop_back();
        }
        if (!result.text.empty() && static_cast<unsigned char>(result.text.back()) >= 0xC0) {
            result.text.pop_back();
        }
    }
    return result;
}

}  // namespace smz
