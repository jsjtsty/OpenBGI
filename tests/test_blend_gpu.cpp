#include <doctest/doctest.h>

#include <map>

#include "render/gpu_blend.hpp"
#include "support.hpp"

using namespace obgi;

// The shader must reproduce the scalar path of the original CPU kernels bit for bit; the expected values come from
// reference formulas that were checked against the real kernels under Unicorn (analysis/混合公式.md).
static void check_driver(const char* driver);

// Every backend that exists on this machine must give identical, original-exact results.
TEST_CASE("gpu blend kernels match the original CPU kernels") {
    std::size_t ran = 0;
    for (const char* driver : {"vulkan", "direct3d12", "metal"}) {
        auto probe = GpuBlendRunner::create(driver);
        if (!probe) {
            MESSAGE(std::string(driver), ": unavailable (", probe.error().message, ")");
            continue;
        }
        probe->reset();
        SUBCASE(driver) { check_driver(driver); }
        ++ran;
    }
    if (!ran) MESSAGE("no usable GPU backend, skipping");
}

static void check_driver(const char* driver) {
    auto runner = GpuBlendRunner::create(driver);
    REQUIRE(runner.has_value());
    MESSAGE("GPU driver: ", std::string((*runner)->driver_name()));

    auto rows = obgi_test::read_tsv("blend_cases.tsv");
    REQUIRE(!rows.empty());
    std::vector<BlendCase> cases;
    std::vector<std::uint32_t> expected;
    cases.reserve(rows.size());
    for (const auto& r : rows) {
        BlendCase c;
        c.mode = static_cast<std::uint8_t>(std::stoul(r[0], nullptr, 16));
        c.src_format = static_cast<std::uint8_t>(std::stoul(r[1]));
        c.dst_format = static_cast<std::uint8_t>(std::stoul(r[2]));
        c.p = static_cast<std::uint16_t>(std::stoul(r[3]));
        c.src = static_cast<std::uint32_t>(std::stoul(r[4], nullptr, 16));
        c.dst = static_cast<std::uint32_t>(std::stoul(r[5], nullptr, 16));
        cases.push_back(c);
        expected.push_back(static_cast<std::uint32_t>(std::stoul(r[6], nullptr, 16)));
    }

    auto got = (*runner)->run(cases);
    REQUIRE_MESSAGE(got.has_value(), std::string(got ? "" : got.error().message));
    REQUIRE(got->size() == cases.size());

    constexpr std::uint32_t kMask[4] = {0xFFFF, 0xFFFFFF, 0xFFFFFFFF, 0xFF};
    std::map<std::string, int> failures; // mode/format -> count
    std::size_t bad = 0;
    for (std::size_t i = 0; i < cases.size(); ++i) {
        const BlendCase& c = cases[i];
        std::uint32_t g = (*got)[i] & kMask[c.dst_format], e = expected[i];
        // Colour of a fully transparent result pixel is invisible (the original CPU path may disagree there).
        bool transparent_dst_ok = (c.mode <= 1 || c.mode == 0x20) && c.dst_format == 2 && (e >> 24) == 0 && (g >> 24) == 0;
        if (g == e || transparent_dst_ok) continue;
        ++bad;
        char key[64];
        std::snprintf(key, sizeof key, "mode %02X %u->%u", c.mode, c.src_format, c.dst_format);
        if (failures[key]++ == 0)
            MESSAGE(key, " P=", c.p, " src=", std::hex, c.src, " dst=", c.dst, " got=", g, " expected=", e);
    }
    CHECK(bad == 0);
    MESSAGE("compared ", cases.size(), " cases, ", bad, " mismatches");
}
