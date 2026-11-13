#pragma once
#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "sha256.hpp"
#include "vfs/arc.hpp"

namespace obgi_test {

namespace fs = std::filesystem;

// Rows of a tab-separated vector file, header skipped.
inline std::vector<std::vector<std::string>> read_tsv(const char* name) {
    std::ifstream f(fs::path(OPENBGI_VECTOR_DIR) / name);
    REQUIRE_MESSAGE(f.good(), name);
    std::vector<std::vector<std::string>> rows;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::vector<std::string> cols;
        std::istringstream ss(line);
        std::string c;
        while (std::getline(ss, c, '\t')) cols.push_back(c);
        if (!cols.empty()) rows.push_back(std::move(cols));
    }
    return rows;
}

// The original-vector RGB remains useful; source-over alpha intentionally uses
// the mathematical coverage instead of the native kernel's banding-prone OR.
inline std::uint32_t corrected_over_alpha(std::uint32_t expected, unsigned mode, unsigned sf, unsigned df,
                                          unsigned p, std::uint32_t src, std::uint32_t dst) {
    if ((mode != 0 && mode != 1 && mode != 0x20) || sf != 2 || df != 2) return expected;
    const unsigned sa = src >> 24, da = dst >> 24;
    if (sa == 0 || (mode == 0 && sa == 255)) return expected;
    const unsigned e = sa * (mode == 0 ? 256 : 256 - p);
    const unsigned coverage = (e + ((da * (65536 - e)) >> 8)) >> 8;
    return (expected & 0x00ffffffu) | (coverage << 24);
}

inline fs::path game_dir() { return fs::path(OPENBGI_GAME_DIR); }

inline std::string sha(std::span<const std::uint8_t> d) { return sha256_hex(d); }

} // namespace obgi_test
