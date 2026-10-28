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

inline fs::path game_dir() { return fs::path(OPENBGI_GAME_DIR); }

inline std::string sha(std::span<const std::uint8_t> d) { return sha256_hex(d); }

} // namespace obgi_test
