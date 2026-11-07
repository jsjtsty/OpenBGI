#include <doctest/doctest.h>
#include <array>

#include "render/display.hpp"
#include "support.hpp"

using namespace obgi;

namespace {

struct Rig {
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Display> display;
};

// A headless renderer + display on the named backend, or an empty Rig when it does not exist here.
Rig make_rig(const char* driver) {
    RendererOptions ro;
    ro.headless = true;
    ro.driver = driver;
    auto r = Renderer::create(ro);
    if (!r) {
        MESSAGE(std::string(driver ? driver : "default"), ": unavailable (", r.error().message, ")");
        return {};
    }
    auto d = Display::create(**r);
    REQUIRE_MESSAGE(d.has_value(), std::string(d ? "" : d.error().message));
    return {std::move(*r), std::move(*d)};
}

constexpr const char* kDrivers[] = {"vulkan", "direct3d12", "metal"};

} // namespace

TEST_CASE("background crossfade layers retain their order when the upper layer was created first" * doctest::test_suite("gpu")) {
    auto rig = make_rig(nullptr);
    REQUIRE(rig.display);
    auto& display = *rig.display;
    REQUIRE(display.create_slot(1, 2, 2, SlotFormat::Bgrx));
    REQUIRE(display.fill_slot(1, 0x00FF0000));
    REQUIRE(display.create_slot(2, 2, 2, SlotFormat::Bgrx));
    REQUIRE(display.fill_slot(2, 0x000000FF));
    auto& upper = display.sprite(Display::kBackgroundUpper);
    upper.background = true; upper.slot = 1; upper.alpha = 128;
    auto& lower = display.sprite(Display::kBackgroundLower);
    lower.background = true; lower.slot = 2;
    REQUIRE(display.compose());
    auto pixels = display.read_screen(); REQUIRE(pixels);
    CHECK(((*pixels)[0] >> 16 & 255) > 0);
    CHECK(((*pixels)[0] & 255) > 0);
}

TEST_CASE("slot fill, clipped blit and compose" * doctest::test_suite("gpu")) {
    for (const char* driver : kDrivers) {
        Rig rig = make_rig(driver);
        if (!rig.display) continue;
        SUBCASE(driver) {
            Display& d = *rig.display;
            REQUIRE(d.create_slot(1, 8, 8, SlotFormat::Bgrx).has_value());
            std::vector<std::uint32_t> src(4 * 4);
            for (std::uint32_t i = 0; i < src.size(); ++i) src[i] = 0x010101u * (i + 1);
            REQUIRE(d.upload_slot(2, 4, 4, SlotFormat::Bgrx, src).has_value());
            REQUIRE(d.fill_slot(1, 0x112233).has_value());

            // copy (mode 0 with equal opaque formats): 4x4 source at (6,6) lands clipped to 2x2
            REQUIRE(d.blit(1, 6, 6, 2, 0, 0, 4, 4, 0, 0x100).has_value());
            // and at (-2,-1): the top-left 2x3 of the destination show source (2..3, 1..3)
            REQUIRE(d.blit(1, -2, -1, 2, 0, 0, 4, 4, 0, 0x100).has_value());
            auto px = d.read_slot(1);
            REQUIRE(px.has_value());
            auto at = [&](int x, int y) { return (*px)[std::size_t(y) * 8 + x]; };
            CHECK(at(7, 7) == src[1 * 4 + 1]);
            CHECK(at(6, 6) == src[0]);
            CHECK(at(0, 0) == src[1 * 4 + 2]);
            CHECK(at(1, 2) == src[3 * 4 + 3]);
            CHECK(at(5, 5) == 0x112233u);
            CHECK(at(2, 0) == 0x112233u);

            // same-slot blit goes through a copy
            REQUIRE(d.blit(1, 3, 3, 1, 0, 0, 2, 2, 0, 0x100).has_value());
            auto px2 = d.read_slot(1);
            REQUIRE(px2.has_value());
            CHECK((*px2)[3 * 8 + 3] == (*px)[0]);
            CHECK((*px2)[4 * 8 + 4] == (*px)[1 * 8 + 1]);

            CHECK_FALSE(d.blit(1, 0, 0, 99, 0, 0, 1, 1, 0, 0x100).has_value()); // no such slot
            CHECK_FALSE(d.blit(1, 0, 0, 2, 0, 0, 1, 1, 0x7F, 0x100).has_value()); // mode not ported
            CHECK_FALSE(d.blit(1, 0, 0, 2, 0, 0, 1, 1, 1, 0x101).has_value()); // alpha too large
            CHECK(d.free_slot(1).has_value());
            CHECK_FALSE(d.free_slot(1).has_value());
        }
    }
}

TEST_CASE("blit reproduces the blend vectors through real image slots" * doctest::test_suite("gpu")) {
    auto rows = obgi_test::read_tsv("blend_cases.tsv");
    REQUIRE(!rows.empty());
    for (const char* driver : kDrivers) {
        Rig rig = make_rig(driver);
        if (!rig.display) continue;
        SUBCASE(driver) {
            Display& d = *rig.display;
            constexpr std::uint32_t kMask[4] = {0xFFFF, 0xFFFFFF, 0xFFFFFFFF, 0xFF};
            std::size_t checked = 0, bad = 0;
            for (std::size_t i = 0; i < rows.size(); i += 61) {
                const auto& r = rows[i];
                std::uint32_t mode = std::stoul(r[0], nullptr, 16), sf = std::stoul(r[1]), df = std::stoul(r[2]);
                std::uint32_t p = std::stoul(r[3]), sv = std::stoul(r[4], nullptr, 16), dv = std::stoul(r[5], nullptr, 16);
                std::uint32_t expected = std::stoul(r[6], nullptr, 16);
                if (!blend_mode_supported(mode)) continue;
                std::uint32_t sp[1] = {sv}, dp[1] = {dv};
                REQUIRE(d.upload_slot(10, 1, 1, static_cast<SlotFormat>(sf), sp).has_value());
                REQUIRE(d.upload_slot(11, 1, 1, static_cast<SlotFormat>(df), dp).has_value());
                REQUIRE(d.blit(11, 0, 0, 10, 0, 0, 1, 1, mode, p).has_value());
                auto got = d.read_slot(11);
                REQUIRE(got.has_value());
                std::uint32_t g = (*got)[0] & kMask[df];
                bool transparent_ok = (mode <= 1 || mode == 0x20) && df == 2 && (expected >> 24) == 0 && (g >> 24) == 0;
                ++checked;
                if (g != expected && !transparent_ok) {
                    if (bad++ == 0)
                        MESSAGE("mode ", std::hex, mode, " sf ", sf, " df ", df, " P ", p, " got ", g, " expected ", expected);
                }
            }
            CHECK(checked > 100);
            CHECK(bad == 0);
        }
    }
}

TEST_CASE("sprites compose by priority and present to the window image" * doctest::test_suite("gpu")) {
    for (const char* driver : kDrivers) {
        Rig rig = make_rig(driver);
        if (!rig.display) continue;
        SUBCASE(driver) {
            Display& d = *rig.display;
            REQUIRE(d.create_slot(1, 100, 100, SlotFormat::Bgrx).has_value());
            REQUIRE(d.create_slot(2, 100, 100, SlotFormat::Bgrx).has_value());
            REQUIRE(d.fill_slot(1, 0x00FF0000).has_value()); // red
            REQUIRE(d.fill_slot(2, 0x000000FF).has_value()); // blue

            Sprite& red = d.sprite(5);
            red.slot = 1, red.x = 10, red.y = 10, red.blend = 0, red.alpha = 0x100, red.priority = 2;
            Sprite& blue = d.sprite(6);
            blue.slot = 2, blue.x = 60, blue.y = 60, blue.blend = 0, blue.alpha = 0x100, blue.priority = 1;

            REQUIRE(d.present().has_value());
            auto frame = rig.renderer->read_back();
            REQUIRE(frame.has_value());
            auto rgba = [&](int x, int y) {
                const std::uint8_t* p = frame->data() + (std::size_t(y) * 800 + x) * 4;
                return std::array<int, 4>{p[0], p[1], p[2], p[3]};
            };
            CHECK(rgba(20, 20) == std::array<int, 4>{255, 0, 0, 255});
            CHECK(rgba(130, 130) == std::array<int, 4>{0, 0, 255, 255});
            // priority 2 (red) is drawn after priority 1 (blue): red wins in the overlap
            CHECK(rgba(70, 70) == std::array<int, 4>{255, 0, 0, 255});
            CHECK(rgba(400, 400) == std::array<int, 4>{0, 0, 0, 255});

            red.visible = false;
            REQUIRE(d.present().has_value());
            frame = rig.renderer->read_back();
            REQUIRE(frame.has_value());
            CHECK(rgba(70, 70) == std::array<int, 4>{0, 0, 255, 255});
        }
    }
}

TEST_CASE("a wipe sprite is revealed through its gray pattern as progress grows" * doctest::test_suite("gpu")) {
    for (const char* driver : kDrivers) {
        Rig rig = make_rig(driver);
        if (!rig.display) continue;
        SUBCASE(driver) {
            Display& d = *rig.display;
            // a white picture, and a pattern that is 0 on the left half and 0x80 on the right half
            std::vector<std::uint32_t> white(16 * 4, 0xFFFFFFFFu), gray(16 * 4);
            for (std::size_t i = 0; i < gray.size(); ++i) gray[i] = (i % 16) < 8 ? 0 : 0x80;
            REQUIRE(d.upload_slot(1, 16, 4, SlotFormat::Bgra, white).has_value());
            REQUIRE(d.upload_slot(2, 16, 4, SlotFormat::Gray8, gray).has_value());
            Sprite& s = d.sprite(5);
            s.slot = 1, s.blend = 1, s.alpha = 0, s.wipe_gray = 2, s.wipe_mode = 0;
            auto shot = [&] {
                REQUIRE(d.present().has_value());
                auto frame = rig.renderer->read_back();
                REQUIRE(frame.has_value());
                return std::array<int, 2>{frame->data()[(1 * 800 + 2) * 4], frame->data()[(1 * 800 + 12) * 4]};
            };
            d.set_progress(5, 0);
            CHECK(shot() == std::array<int, 2>{0, 0}); // nothing revealed
            d.set_progress(5, 0x80); // threshold 0x100: the left half (pattern 0) is fully in, the right half (pattern 0x80) half way
            auto half = shot();
            CHECK(half[0] >= 250);
            CHECK(half[1] > 100);
            CHECK(half[1] < 150);
            d.set_progress(5, 0x100);
            auto full = shot();
            CHECK(full[0] >= 250);
            CHECK(full[1] >= 250); // fully shown (the blend kernel leaves 253 for white)
        }
    }
}
