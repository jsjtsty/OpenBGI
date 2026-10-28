#pragma once
#include <bit>
#include <cstdint>
#include <cstring>
#include <span>

namespace obgi {

static_assert(std::endian::native == std::endian::little, "little-endian hosts only");

using ByteView = std::span<const std::uint8_t>;

inline std::uint32_t load_u32le(const std::uint8_t* p) {
    std::uint32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

} // namespace obgi
