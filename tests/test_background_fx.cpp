#include <doctest/doctest.h>

#include "render/background_fx.hpp"

using namespace obgi::bgfx;

namespace {
Image solid(std::uint32_t w, std::uint32_t h, std::uint32_t px) { return Image{w, h, std::vector<std::uint32_t>(std::size_t{w} * h, px)}; }
} // namespace

TEST_CASE("background crossfade: level 0 is the first picture, 0x100 the second, in between the destination weight") {
    const Image a = solid(2, 2, 0xFF204060), b = solid(2, 2, 0xFFE0A080);
    CHECK(crossfade(a, b, 0).px[0] == 0xFF204060);
    CHECK(crossfade(a, b, 0x100).px[0] == 0xFFE0A080);
    const std::uint32_t mid = crossfade(a, b, 0x80).px[0]; // w = 64: s + ((d - s) * 64 >> 7)
    CHECK((mid & 0xFF) == 0x60 + ((0x80 - 0x60) * 64 >> 7));
}

TEST_CASE("background toward a colour keeps alpha and darkens or whitens by the level") {
    const Image a = solid(1, 1, 0x80FF8040);
    CHECK(toward_colour(a, false, 0x100).px[0] == 0x80000000);
    CHECK(toward_colour(a, true, 0x100).px[0] == 0x80FFFFFF);
    CHECK(toward_colour(a, false, 0).px[0] == 0x80FF8040);
}

TEST_CASE("background tiled view reads across the four pictures") {
    const Image q0 = solid(4, 3, 1), q1 = solid(4, 3, 2), q2 = solid(4, 3, 3), q3 = solid(4, 3, 4);
    const Image v = tiled({&q0, &q1, &q2, &q3}, 2, 1);
    CHECK(v.px[0] == 1);                    // (2, 1) is in the top left picture
    CHECK(v.px[2] == 2);                    // (4, 1) is the top right
    CHECK(v.px[2 * 4] == 3);                // (2, 3) is the bottom left
    CHECK(v.px[2 * 4 + 2] == 4);            // (4, 3) is the bottom right
}

TEST_CASE("background box blur copies at level 0 and averages a step edge") {
    Image s = solid(8, 1, 0);
    for (int x = 4; x < 8; ++x) s.px[x] = 0xFF;
    CHECK(box_blur(s, 1, 0).px == s.px);
    const Image b = box_blur(s, 1, 1); // window of three
    CHECK((b.px[3] & 0xFF) > 0);
    CHECK((b.px[3] & 0xFF) < 0xFF);
    CHECK((b.px[0] & 0xFF) == 0);
    CHECK((b.px[7] & 0xFF) == 0xFF);
}

TEST_CASE("background mosaic: blocks take the top left pixel; amount 0 copies") {
    Image src = solid(4, 4, 0);
    for (std::uint32_t i = 0; i < 16; ++i) src.px[i] = i + 1;
    Image dst = solid(4, 4, 0);
    mosaic(dst, src, 1, 0); // 2 x 2 blocks
    CHECK(dst.px[0] == 1);
    CHECK(dst.px[1] == 1);
    CHECK(dst.px[4] == 1);
    CHECK(dst.px[2] == 3);
    Image copy = solid(4, 4, 0);
    mosaic(copy, src, 0, 0);
    CHECK(copy.px == src.px);
}

TEST_CASE("background zoom and view keep an identity transform") {
    Image src = solid(6, 4, 0);
    for (std::uint32_t i = 0; i < 24; ++i) src.px[i] = 0xFF000000u | i;
    const Image r = rotate_centred(src, 6, 4, 0x10000, 0);
    CHECK(r.px == src.px);
    const Image v = scale2(src, 6, 4, 0, 0, 6 << 16, 4 << 16, 0x10000, 0x10000);
    CHECK(v.px[0] == src.px[0]);
    CHECK(v.px[5 + 3 * 6] == src.px[5 + 3 * 6]);
}
