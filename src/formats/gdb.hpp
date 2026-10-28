#pragma once
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// "BURIKO GDB 3.00" global data (BGI.gdb after SDC decoding). Only the container header is known;
// see analysis/存档与全局数据.md. The body is kept opaque until the field layout is reversed.
struct GdbView {
    std::uint32_t total_size = 0;
    ByteView body; // everything after the 0x10-byte magic
};

Result<GdbView> gdb_parse(ByteView decoded);

// The contents of a decoded BGI.gdb, as the engine restores them (FUN_00479260) and saves them (FUN_004790b0):
//   0x00 magic "BURIKO GDB 3.00", 0x10 total size, 0x14/0x18 window position, 0x1C n, 0x20 n bytes of settings,
//   u32 m + m bytes of script-visible global data (80:82 / 80:83), u32 count + NUL-terminated key strings
//   (80:84 / 80:85), u32 count + named bit tables (name NUL, u32 bit count, ceil(bits/8) bytes).
struct GdbData {
    static constexpr std::size_t kAreaSize = 0x100000;
    static constexpr std::size_t kSettingsSize = 0x400;
    std::uint32_t left = 0, top = 0;
    std::vector<std::uint8_t> settings = std::vector<std::uint8_t>(kSettingsSize, 0);
    std::vector<std::uint8_t> area = std::vector<std::uint8_t>(kAreaSize, 0);
    std::vector<std::string> keys; // in insertion order, without duplicates
    struct BitTable {
        std::string name;
        std::uint32_t bits = 0;
        std::vector<std::uint8_t> data;
    };
    std::vector<BitTable> tables;
};

// Reads the version 3.00 layout; anything else (other versions, inconsistent sizes) is a format error.
Result<GdbData> gdb_decode(ByteView decoded);
std::vector<std::uint8_t> gdb_encode(const GdbData& data);

} // namespace obgi
