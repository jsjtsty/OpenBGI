#include <doctest/doctest.h>

#include <array>
#include <cstring>

#include "bindings/graphics_subops.hpp"
#include "bindings/panel.hpp"
#include "bindings/message_subops.hpp"
#include "bindings/system_subops.hpp"
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

TEST_CASE("window snapshots include painted panel buttons" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(0x10000u, 32, 32, SlotFormat::Bgra));
    REQUIRE(d.fill_slot(0x10000u, 0));
    REQUIRE(d.create_slot(1, 2, 2, SlotFormat::Bgra));
    REQUIRE(d.fill_slot(1, 0xFFFF0000u));
    constexpr Word window = 0xB0000000u, button = 0x7D000001u;
    d.sprite(window).slot = 0x10000u; d.sprite(button).slot = 1;
    SystemState state; state.display = &d; state.planes[0] = SystemState::Plane{};
    state.planes[0]->width = state.planes[0]->height = 32;
    auto& panel = state.icon_controls[42]; panel.window = window; panel.paint_only = true;
    panel.groups.emplace_back(); panel.groups.back().buttons.emplace_back();
    auto& b = panel.groups.back().buttons.back(); b.shown = true; b.sprite = button; b.x = 4; b.y = 5;
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    MemProvider provider; provider.set(Asm().i32(2).i32(window).sub(0x90, 0x83).ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    REQUIRE(vm.spawn(config, "test", "capture")); REQUIRE(vm.run_frame(0));
    auto captured = d.read_slot(2); REQUIRE(captured);
    CHECK(((*captured)[5 * 32 + 4] & 0x00FFFFFFu) == 0x00FF0000u);
}

TEST_CASE("painting a panel replaces old window sprites" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    constexpr Word window = 0xB0000000u, old_sprite = 0x7D001000u, other_sprite = 0x7D002000u;
    d.sprite(window); d.sprite(old_sprite); d.sprite(other_sprite);
    SystemState state; state.display = &d;
    auto& old = state.icon_controls[1]; old.window = window;
    old.groups.emplace_back(); old.groups[0].buttons.emplace_back(); old.groups[0].buttons[0].sprite = old_sprite;
    auto& other = state.icon_controls[2]; other.window = window + 1;
    other.groups.emplace_back(); other.groups[0].buttons.emplace_back(); other.groups[0].buttons[0].sprite = other_sprite;
    SubopRegistry registry; MemProvider provider; provider.set(Asm().ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    auto thread = vm.spawn(config, "test", "paint"); REQUIRE(thread);
    Ctx c(vm, **thread);
    // One group with one hidden button is a valid, empty replacement panel.
    c.write(0x200, 1, 4); c.write(0x204, 0x300, 4);
    c.write(0x300, 1, 4); c.write(0x304, 1, 4); c.write(0x308, 0x400, 4);
    REQUIRE(c.ok());
    CHECK(panel_paint(state, c, window, 0x200, false) == 0);
    CHECK(d.find_sprite(old_sprite) == nullptr);
    CHECK(old.groups[0].buttons[0].sprite == 0);
    CHECK(d.find_sprite(other_sprite) != nullptr);
}

TEST_CASE("panel uses the mouse position at the click, even after a move" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 4, 4, SlotFormat::Bgra));
    constexpr Word window = 0xB0000000u, button = 0x7D000001u;
    d.sprite(window).visible = true;
    auto& sp = d.sprite(button); sp.slot = 1; sp.x = 4; sp.y = 5;
    SystemState s; s.display = &d;
    auto& panel = s.icon_controls[1]; panel.window = window;
    panel.groups.emplace_back(); panel.groups.back().buttons.emplace_back();
    auto& b = panel.groups.back().buttons.back(); b.enabled = b.shown = true; b.sprite = button; b.bmp_normal = 1;
    s.mouse_x = 25; s.mouse_y = 25; // later motion in the same event batch
    s.press_input(1); s.click_x = 5; s.click_y = 6; s.click_position_valid = true;
    panel_input(s);
    CHECK(panel.info[1] == 0);
    CHECK(panel.info[2] == 0);
    CHECK(panel.info[3] == 1);
    CHECK(s.input_pending[1] == 0);
}

TEST_CASE("mouse clicks reach buttons outside the keyboard's current group" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 4, 4, SlotFormat::Bgra));
    constexpr Word window = 0xB0000000u, button = 0x7D000001u;
    d.sprite(window).visible = true;
    auto& sp = d.sprite(button); sp.slot = 1; sp.x = 4; sp.y = 5;
    SystemState s; s.display = &d;
    auto& panel = s.icon_controls[1]; panel.window = window; panel.current_group = 0;
    panel.groups.resize(2); panel.groups[1].buttons.emplace_back();
    auto& b = panel.groups[1].buttons.back(); b.enabled = b.shown = true; b.sprite = button; b.bmp_normal = 1;
    MessageInput message(s);
    s.mouse_x = 5; s.mouse_y = 6; s.press_input(1);
    panel_input(s);
    CHECK(panel.info[1] == 1);
    CHECK(panel.info[2] == 0);
    CHECK(panel.info[3] == 1);
    CHECK(message.poll(s) == 0); // the same click cannot advance text
    CHECK(s.input_pending[1] == 0);
}

TEST_CASE("plain panel buttons use the window text area's origin" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 4, 4, SlotFormat::Bgra));
    constexpr Word window = 0xB0000000u, button = 0x7D000001u;
    auto& win = d.sprite(window); win.x = 3; win.y = 4; win.visible = true;
    d.sprite(button).slot = 1;
    SystemState s; s.display = &d; s.planes[0] = SystemState::Plane{};
    s.planes[0]->area = {7, 8, 31, 31};
    auto& panel = s.icon_controls[1]; panel.window = window; panel.basic = true;
    panel.groups.emplace_back(); panel.groups[0].buttons.emplace_back();
    auto& b = panel.groups[0].buttons[0]; b.enabled = b.shown = true; b.sprite = button; b.bmp_normal = 1; b.x = 2; b.y = 3;
    panel_sync(s);
    CHECK(d.find_sprite(button)->x == 12);
    CHECK(d.find_sprite(button)->y == 15);
    s.mouse_x = 13; s.mouse_y = 16; s.press_input(1);
    panel_input(s);
    CHECK(panel.info[1] == 0);
    CHECK(panel.info[2] == 0);
    CHECK(panel.info[3] == 1);
}

TEST_CASE("window frame drawing survives freeing the source bitmap" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(3, 2, 2, SlotFormat::Bgra));
    REQUIRE(d.fill_slot(3, 0xFFFF0000u));
    SystemState state; state.display = &d;
    TextHost text{&d, nullptr}; FontHost fonts;
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    GraphicsHost graphics{&d, nullptr}; register_graphics_subops(registry, graphics);
    register_message_subops(registry, state, text, fonts);
    constexpr Word window = 0xB0000000u;
    Asm a;
    a.i32(32).i32(32).sub(0x90, 0x80);
    a.i32(window).i32(1).sub(0x92, 0x88);
    a.i32(window).i32(4).i32(5).i32(3).i32(0x80).i32(0).sub(0x92, 0x89);
    a.i32(3).sub(0x90, 0x12);
    a.i32(window).sub(0x92, 0x8E).ret();
    MemProvider provider; provider.set(a);
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    REQUIRE(vm.spawn(config, "test", "frame")); REQUIRE(vm.run_frame(0));
    auto pixels = d.read_slot(0x10000u); REQUIRE(pixels);
    CHECK(((*pixels)[5 * 32 + 4] & 0x00FFFFFFu) == 0x00FF0000u);
    CHECK(((*pixels)[0] & 0x00FFFFFFu) == 0);
}

TEST_CASE("showing one window preserves other windows' current visibility" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 2, 2); REQUIRE(display);
    auto& d = **display;
    constexpr Word first = 0xB0000000u, second = 0xB0000001u;
    d.sprite(first).visible = true;
    d.sprite(second).visible = false;
    SystemState state; state.display = &d;
    state.planes[0] = SystemState::Plane{}; state.planes[1] = SystemState::Plane{};
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    MemProvider provider; provider.set(Asm().i32(second).i32(1).sub(0x90, 0x84).ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    REQUIRE(vm.spawn(config, "test", "show")); REQUIRE(vm.run_frame(0));
    CHECK(d.find_sprite(first)->visible);
    CHECK(d.find_sprite(second)->visible);
}

TEST_CASE("destroying a window removes its painted controls" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 2, 2); REQUIRE(display);
    auto& d = **display;
    constexpr Word window = 0xB0000000u, button = 0x7D000001u;
    REQUIRE(d.create_slot(1, 1, 1, SlotFormat::Bgrx));
    d.sprite(window).visible = true;
    d.sprite(button).slot = 1;
    SystemState state; state.display = &d; state.planes[0] = SystemState::Plane{};
    auto& panel = state.icon_controls[42]; panel.window = window; panel.paint_only = true;
    panel.groups.emplace_back(); panel.groups.back().buttons.emplace_back();
    panel.groups.back().buttons.back().sprite = button;
    panel_sync(state); CHECK(d.find_sprite(button)->visible);
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    MemProvider provider; provider.set(Asm().i32(window).sub(0x90, 0x81).ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    REQUIRE(vm.spawn(config, "test", "remove")); REQUIRE(vm.run_frame(0));
    CHECK(d.find_sprite(button) == nullptr);
    CHECK(state.icon_controls.count(42) == 0);
}

TEST_CASE("group attachment preserves visibility and a tween shows its children" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 2, 2); REQUIRE(display);
    auto& d = **display;
    constexpr Word group = 0xF1000000u, child = 0x80000001u;
    auto& g = d.sprite(group); g.slot = 0xFFFFFFFFu; g.visible = false;
    auto& s = d.sprite(child); s.visible = true;
    SystemState state;
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    GraphicsHost host{&d, nullptr}; register_graphics_subops(registry, host);
    MemProvider provider;
    provider.set(Asm().i32(group).i32(child).i32(0).i32(0).sub(0x90, 0xE8).ret());
    Vm attach(provider, registry); ThreadConfig config; config.stack_size = 1024;
    REQUIRE(attach.spawn(config, "test", "attach")); REQUIRE(attach.run_frame(0));
    CHECK(s.visible);
    provider.set(Asm().i32(group).i32(0x80).sub(0x90, 0x32).ret());
    Vm level(provider, registry);
    REQUIRE(level.spawn(config, "test", "level")); REQUIRE(level.run_frame(0));
    CHECK(s.alpha == 0x80);
    d.set_level(group, 0);
    g.flag_bits &= ~1u;
    d.set_fade(group, 0x80);
    CHECK(g.fade == 0);
    CHECK(s.fade == 0x80);
    d.set_fade(group, 0);
    d.set_visible(group, false);
    CHECK_FALSE(s.visible);
    provider.set(Asm().i32(group).i32(0x100).i32(150).i32(60).i32(0).i32(0).sub(0x90, 0x20).ret());
    Vm tween(provider, registry);
    REQUIRE(tween.spawn(config, "test", "tween")); REQUIRE(tween.run_frame(0));
    CHECK(g.visible);
    CHECK(s.visible);
    advance_motions(state, d, 75);
    CHECK(s.alpha > 0);
    CHECK(s.alpha < 0x100);
    d.remove_sprite(group);
    CHECK_FALSE(s.has_master);
}

TEST_CASE("90 04 copies the previous back buffer after the scene changes" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 2, 2); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 2, 2, SlotFormat::Bgrx));
    REQUIRE(d.fill_slot(1, 0x00FF0000));
    auto& sprite = d.sprite(0x80000001); sprite.slot = 1;
    REQUIRE(d.compose());
    REQUIRE(d.fill_slot(1, 0x000000FF));
    REQUIRE(d.snapshot(2));
    auto saved = d.read_slot(2); REQUIRE(saved);
    CHECK((*saved)[0] == 0x00FF0000);
    REQUIRE(d.compose());
    auto current = d.read_screen(); REQUIRE(current);
    CHECK((*current)[0] == 0x000000FF);
}

TEST_CASE("90 05 snapshots through an inclusive priority without overwriting the live frame" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 2, 2); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 2, 2, SlotFormat::Bgrx));
    REQUIRE(d.fill_slot(1, 0x00FF0000));
    REQUIRE(d.create_slot(2, 2, 2, SlotFormat::Bgrx));
    REQUIRE(d.fill_slot(2, 0x000000FF));
    auto& red = d.sprite(0x80000001); red.slot = 1; red.priority = 5;
    auto& blue = d.sprite(0x80000002); blue.slot = 2; blue.priority = 7;
    auto& filter = d.sprite(0x90000000); filter.filter_kind = 0; filter.alpha = 256; filter.priority = 8;
    d.draw_limit = 6;
    REQUIRE(d.compose());
    GraphicsHost host{display->get(), nullptr};
    SubopRegistry registry; register_core_subops(registry); register_graphics_subops(registry, host);
    MemProvider provider;
    Asm a;
    a.i32(3793).i32(5).sub(0x90, 0x05).i32(10).i32(7).sub(0x90, 0x05).i32(11).i32(8).sub(0x90, 0x05).ret();
    provider.set(a);
    Vm machine(provider, registry);
    ThreadConfig config; config.stack_size = 1024;
    REQUIRE(machine.spawn(config, "test", "snapshots"));
    REQUIRE(machine.run_frame(0));
    auto low = d.read_slot(3793), inclusive = d.read_slot(10), covered = d.read_slot(11), live = d.read_screen();
    REQUIRE(low); REQUIRE(inclusive); REQUIRE(covered); REQUIRE(live);
    CHECK((*low)[0] == 0x00FF0000);
    CHECK((*inclusive)[0] == 0x000000FF);
    CHECK((*covered)[0] == 0);
    CHECK((*live)[0] == 0);
}

TEST_CASE("graphics sub-opcodes draw a script-loaded picture through the display" * doctest::test_suite("game")) {
    namespace fs = std::filesystem;
    REQUIRE(fs::exists(obgi_test::game_dir() / "data02200.arc"));

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

TEST_CASE("graphics sub-opcodes reject bad arguments" * doctest::test_suite("gpu")) {
    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    REQUIRE(renderer);
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

TEST_CASE("tile maps show the tiles of their terrain and leave empty cells clear" * doctest::test_suite("gpu")) {
    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    REQUIRE(renderer);
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

TEST_CASE("vector maps: created by 90:11, filled by 91:10, read by 90:1A" * doctest::test_suite("gpu")) {
    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    REQUIRE(renderer);
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

    Asm a;
    a.i32(1).i32(4).i32(4).i32(1).sub(0x90, 0x11);  // slot 1: 4 x 4 picture, red
    a.i32(1).i32(0x00FF0000).sub(0x90, 0x13);
    a.i32(2).i32(4).i32(4).i32(1).sub(0x90, 0x11);  // slot 2: the result
    a.i32(3).i32(4).i32(4).i32(4).sub(0x90, 0x11);  // slot 3: a vector map (mode 4)
    a.i32(3).i32(2).i32(0).i32(4).i32(4).sub(0x91, 0x10); // the rectangle starting at x = 2: every vector (2, 0) pixels
    a.i32(2).i32(1).i32(3).i32(0xFFFFFFFFu).i32(0x100).i32(0).sub(0x90, 0x1A);
    a.ret();
    provider.set(a);
    ThreadConfig cfg;
    cfg.stack_size = 0x1000;
    cfg.program_capacity = 0x10000;
    REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
    auto st = vm.run_frame(0);
    REQUIRE_MESSAGE(st.has_value(), std::string(st ? "" : st.error().message));
    auto map = (*display)->vecmaps.find(3);
    REQUIRE(map != (*display)->vecmaps.end());
    CHECK(map->second.px[0] == 32);
    auto px = (*display)->read_slot(2);
    REQUIRE(px.has_value());
    CHECK(((*px)[0] & 0x00FFFFFF) == 0x00FF0000); // x + 2 is inside the picture
    CHECK(((*px)[1] & 0x00FFFFFF) == 0x00FF0000);
    CHECK(((*px)[2] & 0x00FFFFFF) == 0);          // x + 2 is outside: nothing
}

TEST_CASE("ripple sprite 90:5B and ripple definitions run through the display" * doctest::test_suite("gpu")) {
    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    REQUIRE(renderer);
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

    Asm a;
    a.i32(1).i32(8).i32(8).i32(1).sub(0x90, 0x11); // slot 1: an 8 x 8 red picture
    a.i32(1).i32(0x00FF0000).sub(0x90, 0x13);
    a.i32(3).i32(8).i32(8).i32(6).sub(0x90, 0x11); // slot 3: a vector + distance map
    a.i32(3).i32(0).sub(0x92, 0x11);                // linear, type 0
    a.i32(0).i32(1).i32(4).i32(1).i32(8).sub(0x92, 0x00); // ripple 0: period 4 pairs, 8 rings
    a.i32(1).i32(10).i32(20).i32(1).i32(3).i32(2).i32(0).i32(0x100).i32(0).i32(5).sub(0x90, 0x5B);
    a.ret();
    provider.set(a);
    ThreadConfig cfg;
    cfg.stack_size = 0x1000;
    cfg.program_capacity = 0x10000;
    REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
    auto st = vm.run_frame(0);
    REQUIRE_MESSAGE(st.has_value(), std::string(st ? "" : st.error().message));
    REQUIRE((*display)->present().has_value());
    const Sprite* sp = (*display)->find_sprite(1);
    REQUIRE(sp != nullptr);
    CHECK(sp->ripple_map == 3);
    CHECK(sp->rings == 2);
    CHECK((*display)->ripples[0].defined);
    // a bad ripple number or ring count is a script error
    Asm bad;
    bad.i32(1).i32(10).i32(20).i32(1).i32(3).i32(0).i32(0).i32(0x100).i32(0).i32(5).sub(0x90, 0x5B).ret();
    provider.set(bad);
    REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
    CHECK_FALSE(vm.run_frame(0).has_value());
}

TEST_CASE("an effector displaces everything drawn below it" * doctest::test_suite("gpu")) {
    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    REQUIRE(renderer);
    auto display = Display::create(**renderer);
    REQUIRE(display.has_value());
    ArcResourceFinder finder(obgi_test::game_dir());
    ArcImageSource images(finder);
    GraphicsHost host{display->get(), &images};
    SubopRegistry registry;
    register_core_subops(registry);
    register_graphics_subops(registry, host);
    SystemState state;
    register_plane_subops(registry, state, display->get()); // the effectors live with the plane settings
    MemProvider provider;
    Vm vm(provider, registry);

    const Word sw = (*display)->screen_width(), sh = (*display)->screen_height();
    constexpr Word eff = 0x91000000u;
    Asm a;
    a.i32(1).i32(8).i32(8).i32(1).sub(0x90, 0x11); // a red 8 x 8 picture drawn at (10, 20)
    a.i32(1).i32(0x00FF0000).sub(0x90, 0x13);
    a.i32(0x80000001u).i32(10).i32(20).i32(1).i32(1).i32(0).i32(1).sub(0x90, 0x56);
    a.i32(3).i32(sw).i32(sh).i32(4).sub(0x90, 0x11); // a screen-sized vector map reading 5 pixels to the right
    a.i32(3).i32(5).i32(0).i32(sw).i32(sh).sub(0x91, 0x10);
    a.sub(0x91, 0x60);                                // the handle stays on the stack
    a.i32(eff).i32(3).i32(0xFFFFFFFFu).i32(0x100).i32(0).i32(100).sub(0x91, 0x65);
    a.i32(eff).i32(1).sub(0x91, 0x64);
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
    CHECK(((*screen)[22 * sw + 7] & 0xFFFFFF) == 0xFF0000);  // the picture moved from x 10..17 to x 5..12
    CHECK(((*screen)[22 * sw + 14] & 0xFFFFFF) != 0xFF0000);
}
