#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace obgi {
// sysprg/environ._bp +0x163: AUTO delay table indexed by global byte 0x8a.
inline std::optional<std::uint32_t> eustia_auto_delay(std::uint8_t setting) {
    constexpr std::array<std::uint32_t, 11> delays = {4490, 3740, 2990, 2490, 1990, 1490, 990, 490, 240, 120, 0};
    if (setting >= delays.size()) return std::nullopt;
    return delays[setting];
}
} // namespace obgi
