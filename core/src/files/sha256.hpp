#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace smz {

// SHA-256 (FIPS 180-4). Used as the content id of every imported file, so the same
// file is recognized even after it is renamed or moved, and never analyzed twice.
class Sha256 {
public:
    Sha256();

    void update(const void* data, std::size_t size);

    // Finishes the hash and returns it as 64 lowercase hex characters.
    // The object must not be updated afterwards.
    std::string hex_digest();

private:
    void process_block(const std::uint8_t* block);

    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffer_size_ = 0;
    std::uint64_t total_bytes_ = 0;
};

}  // namespace smz
