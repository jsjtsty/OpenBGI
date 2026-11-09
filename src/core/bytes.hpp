#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>
#include <memory>

namespace obgi {

static_assert(std::endian::native == std::endian::little, "little-endian hosts only");

using ByteView = std::span<const std::uint8_t>;

struct OwnedBytes {
    std::shared_ptr<const void> owner;
    ByteView bytes;
};

inline std::uint32_t load_u32le(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

} // namespace obgi
