#include <doctest/doctest.h>

#include <map>

#include "formats/bm.hpp"
#include "formats/bw.hpp"
#include "formats/cbg.hpp"
#include "formats/gdb.hpp"
#include "formats/sdc.hpp"
#include "support.hpp"

using namespace obgi_test;

namespace {
bool have_game() {
    if (fs::exists(game_dir())) return true;
    MESSAGE("game directory not found, skipping: ", game_dir().string());
    return false;
}

// Opens each archive once.
struct ArcCache {
    std::map<std::string, obgi::Arc> arcs;
    obgi::ByteView member(const std::string& arc, const std::string& name) {
        auto it = arcs.find(arc);
        if (it == arcs.end()) {
            auto a = obgi::Arc::open(game_dir() / arc);
            REQUIRE_MESSAGE(a.has_value(), arc);
            it = arcs.emplace(arc, std::move(*a)).first;
        }
        const obgi::ArcEntry* e = it->second.find(name);
        REQUIRE_MESSAGE(e != nullptr, arc, "/", name);
        return it->second.data(*e);
    }
};
} // namespace

TEST_CASE("cbg matches the Python reference (itself checked against the original exe)") {
    if (!have_game()) return;
    ArcCache cache;
    auto rows = read_tsv("cbg_manifest.tsv");
    REQUIRE(!rows.empty());
    for (const auto& r : rows) {
        auto img = obgi::cbg_decode(cache.member(r[0], r[1]));
        REQUIRE_MESSAGE(img.has_value(), r[1], ": ", img ? "" : img.error().message);
        CHECK_MESSAGE(img->width == std::stoul(r[2]), r[1]);
        CHECK_MESSAGE(img->height == std::stoul(r[3]), r[1]);
        CHECK_MESSAGE(img->bits_per_pixel == std::stoul(r[4]), r[1]);
        CHECK_MESSAGE(sha(img->to_rgba()) == r[5], r[0], "/", r[1]);
    }
}

TEST_CASE("cbg rejects garbage") {
    std::uint8_t junk[64] = {};
    CHECK_FALSE(obgi::cbg_decode(junk).has_value());
}

TEST_CASE("bm frames match the Python reference (checked against the original decoder)") {
    if (!have_game()) return;
    ArcCache cache;
    std::map<std::string, obgi::BmDecoder> decoders;
    for (const auto& r : read_tsv("bm_manifest.tsv")) {
        std::string key = r[0] + "/" + r[1];
        auto it = decoders.find(key);
        if (it == decoders.end()) {
            auto d = obgi::BmDecoder::open(cache.member(r[0], r[1]));
            REQUIRE_MESSAGE(d.has_value(), key);
            it = decoders.emplace(key, std::move(*d)).first;
        }
        std::uint32_t frame = std::stoul(r[2]);
        auto ok = it->second.decode_frame(frame);
        REQUIRE_MESSAGE(ok.has_value(), key, " frame ", frame, ": ", ok ? "" : ok.error().message);
        CHECK_MESSAGE(sha(it->second.canvas()) == r[3], key, " frame ", frame);
    }
}

TEST_CASE("sdc and gdb decode global data") {
    if (!have_game()) return;
    for (const auto& r : read_tsv("sdc_manifest.tsv")) {
        std::string rel(r[0]);
        auto mf = obgi::MappedFile::open(game_dir() / std::filesystem::path(std::u8string(rel.begin(), rel.end())));
        REQUIRE_MESSAGE(mf.has_value(), rel);
        auto out = obgi::sdc_decode(mf->bytes());
        REQUIRE_MESSAGE(out.has_value(), rel, ": ", out ? "" : out.error().message);
        CHECK(out->consumed == std::stoull(r[1]));
        CHECK(out->data.size() == std::stoull(r[2]));
        CHECK(sha(out->data) == r[3]);
        auto gdb = obgi::gdb_parse(out->data);
        CHECK_MESSAGE(gdb.has_value(), rel, ": ", gdb ? "" : gdb.error().message);
        // the structured reader loses nothing: writing it back gives the same bytes
        auto data = obgi::gdb_decode(out->data);
        REQUIRE_MESSAGE(data.has_value(), rel, ": ", data ? "" : data.error().message);
        CHECK_MESSAGE(obgi::gdb_encode(*data) == out->data, rel);
        CHECK(data->area.size() == obgi::GdbData::kAreaSize);
    }
}

TEST_CASE("sdc rejects a corrupted payload") {
    std::uint8_t d[0x30] = {'S', 'D', 'C', ' ', 'F', 'O', 'R', 'M', 'A', 'T', ' ', '1', '.', '0', '0', 0};
    d[0x14] = 4; // payload bytes
    d[0x1c] = 99; // wrong checksum
    CHECK_FALSE(obgi::sdc_decode(d).has_value());
}

TEST_CASE("every bw member in the game archives parses") {
    if (!have_game()) return;
    std::size_t count = 0;
    for (const auto& de : fs::directory_iterator(game_dir())) {
        if (de.path().extension() != ".arc") continue;
        auto arc = obgi::Arc::open(de.path());
        REQUIRE(arc.has_value());
        for (const auto& e : arc->entries()) {
            obgi::ByteView d = arc->data(e);
            if (d.size() < 8 || std::memcmp(d.data() + 4, "bw  ", 4) != 0) continue;
            auto h = obgi::bw_parse(d);
            REQUIRE_MESSAGE(h.has_value(), de.path().filename().string(), "/", e.name);
            CHECK(h->stream.size() == d.size() - 0x40);
            CHECK(h->model == 3);
            CHECK(h->channels >= 1);
            CHECK(std::memcmp(h->stream.data(), "OggS", 4) == 0);
            ++count;
        }
    }
    MESSAGE("parsed ", count, " bw members");
    CHECK(count == 7520);
}
