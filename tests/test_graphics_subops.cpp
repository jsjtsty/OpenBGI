#include <doctest/doctest.h>

#include <array>
#include <cstring>

#include "bindings/graphics_subops.hpp"
#include "support.hpp"
#include "vm/vm.hpp"

using namespace obgi;
using namespace obgi::vm;

namespace {

struct Asm {
    std::vector<std::uint8_t> b;
    Asm& raw(std::initializer_list<int> bytes) {
        for (int x : bytes) b.push_back(static_cast<std::uint8_t>(x));
        return *this;
    }
    Asm& i32(Word v) { return raw({0x02, int(v & 255), int((v >> 8) & 255), int((v >> 16) & 255), int(v >> 24)}); }
    Asm& sub(int op, int sub) { return raw({op, sub}); }
    Asm& ret() { return raw({0x17}); }
};

class MemProvider final : public ProgramProvider {
  public:
    std::vector<std::uint8_t> program;
    void set(const Asm& a) {
        program = {8, 0, 0, 0};
        Word n = static_cast<Word>(a.b.size());
        for (int i = 0; i < 4; ++i) program.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
        program.insert(program.end(), a.b.begin(), a.b.end());
    }
    Result<std::vector<std::uint8_t>> load(std::string_view, std::string_view) override { return program; }
};

void put_string(Vm& vm, Word addr, const char* s) {
    REQUIRE(vm.global.write(addr, std::strlen(s) + 1, reinterpret_cast<const std::uint8_t*>(s)));
}

} // namespace

TEST_CASE("graphics sub-opcodes draw a script-loaded picture through the display") {
    namespace fs = std::filesystem;
    if (!fs::exists(obgi_test::game_dir() / "data02200.arc")) return;

    for (const char* driver : {"vulkan", "direct3d12", "metal"}) {
        RendererOptions ro;
        ro.headless = true;
        ro.driver = driver;
        auto renderer = Renderer::create(ro);
        if (!renderer) continue;
        SUBCASE(driver) {
            auto display = Display::create(**renderer);
            REQUIRE(display.has_value());
            ArcResourceFinder finder(obgi_test::game_dir());
            ArcImageSource images(finder);
            GraphicsHost host{display->get(), &images};

            SubopRegistry registry;
            register_core_subops(registry);
            register_graphics_subops(registry, host);
            MemProvider provider;
            Vm vm(provider, registry);

            // slot 3 <- picture; slot 4 <- 64x64 BGRX filled green; sprites 1 (picture) and 2 (green square)
            put_string(vm, 0x100, "data02200.arc");
            put_string(vm, 0x120, "mik01A_001");
            Asm a;
            a.i32(3).i32(0x100).i32(0x120).sub(0x90, 0x10);
            a.i32(4).i32(64).i32(64).i32(1).sub(0x90, 0x11);
            a.i32(4).i32(0x0000FF00).sub(0x90, 0x13);
            // set sprite: id x y slot blend alpha priority
            a.i32(1).i32(100).i32(0).i32(3).i32(1).i32(0).i32(1).sub(0x90, 0x56); // mode 1: alpha 0 = opaque
            a.i32(2).i32(10).i32(20).i32(4).i32(0).i32(0x100).i32(2).sub(0x90, 0x56);
            // move the green square, then query nothing: 90:33 id x y
            a.i32(2).i32(30).i32(40).sub(0x90, 0x33);
            a.ret();
            provider.set(a);

            ThreadConfig cfg;
            cfg.stack_size = 0x1000;
            cfg.program_capacity = 0x10000;
            REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
            auto st = vm.run_frame(0);
            REQUIRE_MESSAGE(st.has_value(), std::string(st ? "" : st.error().message));

            REQUIRE(display->get()->slot(3));
            CHECK(display->get()->slot(3)->width == 600);
            CHECK(display->get()->slot(3)->height == 884);
            REQUIRE(display->get()->find_sprite(2));
            CHECK(display->get()->find_sprite(2)->x == 30);

            REQUIRE((*display)->present().has_value());
            auto screen = (*display)->read_screen();
            REQUIRE(screen.has_value());
            CHECK((*screen)[45 * 800 + 35] == 0x0000FF00u); // inside the green square (priority 2, drawn last)
            CHECK((*screen)[(45 + 64) * 800 + 35] != 0x0000FF00u);
            std::size_t lit = 0;
            for (std::uint32_t px : *screen) lit += (px & 0xFFFFFF) != 0;
            CHECK(lit > 10000); // the picture drew something
        }
    }
}

TEST_CASE("graphics sub-opcodes reject bad arguments") {
    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    if (!renderer) return;
    auto display = Display::create(**renderer);
    REQUIRE(display.has_value());
    ArcResourceFinder finder(obgi_test::game_dir());
            ArcImageSource images(finder);
    GraphicsHost host{display->get(), &images};
    SubopRegistry registry;
    register_core_subops(registry);
    register_graphics_subops(registry, host);
    MemProvider provider;
    Vm vm(provider, registry);

    auto run = [&](const Asm& a) {
        provider.set(a);
        ThreadConfig cfg;
        cfg.stack_size = 0x1000;
        cfg.program_capacity = 0x10000;
        REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
        return vm.run_frame(0);
    };
    CHECK_FALSE(run(Asm().i32(0x4000).i32(8).i32(8).i32(1).sub(0x90, 0x11).ret()).has_value()); // slot out of range
    CHECK_FALSE(run(Asm().i32(1).i32(8).i32(8).i32(9).sub(0x90, 0x11).ret()).has_value());      // bad format
    CHECK_FALSE(run(Asm().i32(7).i32(1).i32(1).i32(1).i32(1).i32(1).i32(1).sub(0x90, 0x56).ret()).has_value()); // no slot 1
    CHECK_FALSE(run(Asm().i32(9).i32(1).i32(1).sub(0x90, 0x33).ret()).has_value());              // no object
}

TEST_CASE("tile maps show the tiles of their terrain and leave empty cells clear") {
    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    if (!renderer) return;
    auto display = Display::create(**renderer);
    REQUIRE(display.has_value());
    ArcResourceFinder finder(obgi_test::game_dir());
    ArcImageSource images(finder);
    GraphicsHost host{display->get(), &images};
    SubopRegistry registry;
    register_core_subops(registry);
    register_graphics_subops(registry, host);
    MemProvider provider;
    Vm vm(provider, registry);

    // a 32 x 16 chipset of two 16 x 16 tiles, the first red; terrain [tile 0, empty]; the view is 2 x 1 tiles
    const std::uint8_t terrain[4] = {0x00, 0x00, 0xFF, 0xFF};
    REQUIRE(vm.global.write(0x200, 4, terrain));
    constexpr Word h = 0xA0000000u; // the first map
    Asm a;
    a.i32(5).i32(32).i32(16).i32(1).sub(0x90, 0x11);
    a.i32(5).i32(0x00FF0000).sub(0x90, 0x13);
    a.sub(0x90, 0x70);                                                          // pushes the handle (left on the stack)
    a.i32(h).i32(2).i32(1).i32(16).i32(16).sub(0x90, 0x76);                     // 2 x 1 tiles of 16 x 16
    a.i32(h).i32(2).i32(1).i32(0x200).sub(0x90, 0x78);                          // terrain
    a.i32(h).i32(10).i32(20).i32(5).i32(1).i32(0).i32(1).sub(0x90, 0x75);       // chipset, at (10, 20)
    a.i32(h).i32(0).i32(0).i32(0).i32(0).i32(0).sub(0x90, 0x79);                // view at the terrain's corner
    a.i32(h).i32(1).sub(0x90, 0x74);                                            // shown
    a.ret();
    provider.set(a);
    ThreadConfig cfg;
    cfg.stack_size = 0x1000;
    cfg.program_capacity = 0x10000;
    REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
    auto st = vm.run_frame(0);
    REQUIRE_MESSAGE(st.has_value(), std::string(st ? "" : st.error().message));
    REQUIRE((*display)->present().has_value());
    auto screen = (*display)->read_screen();
    REQUIRE(screen.has_value());
    CHECK((*screen)[25 * 800 + 15] == 0x00FF0000u); // inside the first tile
    CHECK((*screen)[25 * 800 + 35] != 0x00FF0000u); // the second cell is empty
}
