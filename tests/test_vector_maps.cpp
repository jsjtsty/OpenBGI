#include <doctest/doctest.h>

#include "render/vector_maps.hpp"

using namespace obgi;
using namespace obgi::vecmap;

namespace {
bgfx::Image strip(std::initializer_list<std::uint32_t> px) { return bgfx::Image{static_cast<std::uint32_t>(px.size()), 1, std::vector<std::uint32_t>(px)}; }
Map constant_vector(std::uint32_t w, std::uint32_t h, int dx, int dy) {
    Map m(Mode::Vector, w, h);
    for (auto& v : m.px) v = static_cast<std::uint16_t>(dx) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(dy)) << 16;
    return m;
}
} // namespace

TEST_CASE("vector map generators reject maps of the wrong mode and bad ranges") {
    Map height(Mode::Height, 4, 4);
    CHECK(stretch_range(height, 0, 0, 4, 4) == static_cast<int>(0x80000003));
    Map vec(Mode::Vector, 4, 4);
    CHECK(stretch_range(vec, 0, 0, 0, 4) == static_cast<int>(0x80000005));
    CHECK(ripple(vec, 2, 2, 0, 0, 10) == static_cast<int>(0x80000006));
    CHECK(bend_angle(vec, 2, 2, 0, 5) == static_cast<int>(0x80000008));
    Map vd(Mode::VecDist, 4, 4);
    CHECK(radial(vd, 2, 0, 0, 0) == static_cast<int>(0x80000009));
    CHECK(linear(vd, 4) == static_cast<int>(0x8000000A));
    CHECK(radial(vec, 0, 0, 0, 0) == static_cast<int>(0x80000003));
}

TEST_CASE("stretch range maps the bitmap onto a rectangle") {
    Map vec(Mode::Vector, 4, 4);
    REQUIRE(stretch_range(vec, 2, 0, 4, 4) == 0);
    CHECK(vec.px[0] == 32); // dx = 16 * ox, dy = 0 at every pixel when the rectangle is the bitmap's own size
    CHECK(vec.px[15] == 32);
}

TEST_CASE("vector map from a height field is the signed gradient of the squares") {
    Map height(Mode::Height, 2, 2);
    height.px = {0, 4, 0, 4};
    Map vec(Mode::Vector, 1, 1);
    REQUIRE(from_height(vec, height, 2) == 0);
    CHECK(vec.px[0] == 4);                                       // u = 16 / (2 * 2), v = 0
    CHECK(from_height(vec, Map(Mode::Height, 3, 3), 2) == static_cast<int>(0x80000004));
}

TEST_CASE("sine and random fields fill the whole map") {
    Map vec(Mode::Vector, 4, 4);
    REQUIRE(sine(vec, 4, 1, 2, 4, 0, 0) == 0);
    CHECK(vec.px[0] == (32u << 16)); // ampY 0 leaves dx 0; dy of column 0 is 16 * sin(2 pi (0 + 1) / 4) * ampX = 32
    Map r(Mode::Vector, 2, 2);
    std::uint32_t n = 0;
    REQUIRE(random(r, 3, [&] { return n++ & 0x7FFF; }) == 0);
    for (std::uint32_t v : r.px) {
        CHECK(static_cast<std::int16_t>(v & 0xFFFF) >= -3);
        CHECK(static_cast<std::int16_t>(v & 0xFFFF) <= 3);
    }
}

TEST_CASE("vecdist maps: linear and radial") {
    Map vd(Mode::VecDist, 4, 2);
    REQUIRE(linear(vd, 0) == 0);
    CHECK(vd.px[0] == 0x7FFF);          // type 0 points right
    CHECK(vd.px[(1 * 4 + 2) * 2 + 1] == 4); // distance word is 4 y
    REQUIRE(radial(vd, 0, 0, 0, 0) == 0);
    CHECK(vd.px[3 * 2] == 0x7FFF);      // pixel (3, 0): unit vector (1, 0)
    CHECK(vd.px[3 * 2 + 1] == 12);      // 4 * distance
    CHECK(vd.px[0] == 0);               // the centre: 0 / 0 gives 0
}

TEST_CASE("displace reads the source through the vector map") {
    const bgfx::Image src = strip({1, 2, 3, 4});
    const Map one = constant_vector(4, 1, 16, 0); // one pixel to the right (12.4)
    bgfx::Image out;
    REQUIRE(displace(out, src, one, nullptr, 0x100, 0) == 0);
    CHECK(out.px == std::vector<std::uint32_t>{2, 3, 4, 0});
    REQUIRE(displace(out, src, one, nullptr, 0, 0) == 0);
    CHECK(out.px == src.px);                                     // level 0 copies
    REQUIRE(displace(out, src, one, nullptr, 0x100, 1) == 0);    // bilinear with a whole-pixel vector is the same
    CHECK(out.px == std::vector<std::uint32_t>{2, 3, 4, 0});
    const Map zero = constant_vector(4, 1, 0, 0);
    REQUIRE(displace(out, src, zero, &one, 0x100, 0) == 0);      // level 0x100 mixes fully into the second map
    CHECK(out.px == std::vector<std::uint32_t>{2, 3, 4, 0});
    CHECK(displace(out, src, one, nullptr, 0x101, 0) == 3);
    CHECK(displace(out, src, constant_vector(3, 1, 0, 0), nullptr, 0, 0) == 0xC);
    const Map narrow = constant_vector(3, 1, 0, 0);
    CHECK(displace(out, src, one, &narrow, 0, 0) == 0xD);
}

TEST_CASE("ripple definitions, ring tables and the ripple blitter") {
    RippleDefs defs;
    bool ok = false;
    CHECK(ripple_check(defs, ok, 0, 0, 1) == 0x11);
    CHECK(ripple_check(defs, ok, 9, 0, 1) == 0x10);
    REQUIRE(define_ripple(defs, 0, 2, 100, 1, 2) == 0);          // period 8 pairs, 2 rings: 16 pairs
    CHECK(defs[0].total == 16);
    CHECK(defs[0].table[2 * 2] == 100);                          // sin(2 pi 2 / 8) * 100
    REQUIRE(ripple_check(defs, ok, 0, 0, 2) == 0);
    CHECK(ok);
    REQUIRE(ripple_check(defs, ok, 0, 0, 5) == 0);
    CHECK(!ok);
    std::vector<std::uint32_t> table;
    REQUIRE(ripple_fill(defs, table, 0, 0, 0x100, 2) == 0);
    REQUIRE(table.size() == 8);
    CHECK(table[2] == (100u << 16 | 100u));
    CHECK(ripple_fill(defs, table, 0, 0, 0x100, 5) == 0x12);

    const bgfx::Image src = strip({1, 2, 3, 4});
    Map vd(Mode::VecDist, 4, 1);
    for (std::uint32_t x = 0; x < 4; ++x) vd.px[x * 2] = 0x100; // vx = 0x100, vy = 0, ring 0
    const std::vector<std::uint32_t> ring{0x0100u << 16 | 0x0100u}; // (0x100 * 0x100) >> 16 = 1 pixel
    bgfx::Image out;
    REQUIRE(ripple_blit(out, src, vd, ring) == 0);
    CHECK(out.px == std::vector<std::uint32_t>{2, 3, 4, 0});
    CHECK(ripple_blit(out, src, Map(Mode::VecDist, 3, 1), ring) == 0xC);
}
