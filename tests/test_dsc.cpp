#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "formats/dsc.hpp"
#include "sha256.hpp"
#include "vfs/arc.hpp"

namespace fs = std::filesystem;

TEST_CASE("dsc rejects garbage") {
    std::uint8_t junk[64] = {};
    CHECK_FALSE(obgi::is_dsc(junk));
    CHECK_FALSE(obgi::dsc_decode(junk).has_value());
}

// Byte-exact check against SHA-256 values produced by the original exe's decoder (analysis/DSC格式.md).
TEST_CASE("dsc matches vectors from the original decoder" * doctest::test_suite("game")) {
    fs::path game = OPENBGI_GAME_DIR;
    if (!fs::exists(game)) {
        MESSAGE("game directory not found, skipping: ", game.string());
        return;
    }
    std::ifstream f(fs::path(OPENBGI_VECTOR_DIR) / "dsc_manifest.tsv");
    REQUIRE(f.good());

    std::multiset<std::string> expected; // "arc:stored:decoded:sha"; archives hold some members twice
    std::set<std::string> arcs;
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line)) {
        std::istringstream ss(line);
        std::string arc, stored, decoded, sha;
        std::getline(ss, arc, '\t');
        std::getline(ss, stored, '\t');
        std::getline(ss, decoded, '\t');
        std::getline(ss, sha, '\t');
        expected.insert(arc + ":" + stored + ":" + decoded + ":" + sha);
        arcs.insert(arc);
    }
    REQUIRE(!expected.empty());

    std::size_t matched = 0, mismatched = 0;
    for (const auto& name : arcs) {
        auto arc = obgi::Arc::open(game / name);
        REQUIRE_MESSAGE(arc.has_value(), name);
        for (const auto& e : arc->entries()) {
            obgi::ByteView data = arc->data(e);
            if (!obgi::is_dsc(data)) continue;
            auto out = obgi::dsc_decode(data);
            if (!out) {
                ++mismatched;
                MESSAGE(name, "/", e.name, ": ", out.error().message);
                continue;
            }
            std::string key = name + ":" + std::to_string(e.size) + ":" + std::to_string(out->size()) + ":" +
                              obgi_test::sha256_hex(*out);
            if (auto it = expected.find(key); it != expected.end()) {
                expected.erase(it);
                ++matched;
            } else {
                ++mismatched;
                MESSAGE(name, "/", e.name, ": output differs from the original decoder");
            }
        }
    }
    CHECK(mismatched == 0);
    CHECK(expected.empty());
    MESSAGE("matched ", matched, " members");
}
