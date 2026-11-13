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

TEST_CASE("90:19 without a gray map crossfades rather than layering pictures" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 4, 4); REQUIRE(display);
    auto& d = **display;
    const std::vector<std::uint32_t> old{0xff0000ff, 0xff00ff00};
    const std::vector<std::uint32_t> fresh{0x00000000, 0xffff0000};
    REQUIRE(d.upload_slot(2, 2, 1, SlotFormat::Bgra, fresh));
    GraphicsHost host{&d, nullptr}; SubopRegistry registry; register_graphics_subops(registry, host);
    for (unsigned ratio : {0u, 128u, 256u}) {
        REQUIRE(d.upload_slot(1, 2, 1, SlotFormat::Bgra, old));
        MemProvider provider;
        provider.set(Asm().i32(1).i32(0).i32(0).i32(2).i32(0xffffffffu).i32(0).i32(ratio).sub(0x90, 0x19).ret());
        Vm vm(provider, registry); ThreadConfig cfg; cfg.stack_size = 1024;
        REQUIRE(vm.spawn(cfg, "test", "crossfade")); REQUIRE(vm.run_frame(0));
        auto actual = d.read_slot(1); REQUIRE(actual);
        if (ratio == 0) CHECK(*actual == old);
        if (ratio == 128) { CHECK((*actual)[0] == 0x7f0000ff); CHECK((*actual)[1] == 0xff7f7f00); }
        if (ratio == 256) CHECK(*actual == fresh);
    }
}

TEST_CASE("copied images retain their positioning metadata" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 2, 2, SlotFormat::Bgra));
    REQUIRE(d.fill_slot(1, 0xFF123456));
    REQUIRE(d.create_slot(4, 2, 2, SlotFormat::Bgra));
    GraphicsHost host{&d, nullptr};
    SubopRegistry registry; register_core_subops(registry); register_graphics_subops(registry, host);
    Asm a;
    a.i32(1).i32(370).i32(236).sub(0x92, 0x12);
    a.i32(1).i32(1).sub(0x91, 0x1F);
    a.i32(2).i32(1).sub(0x91, 0x1F);
    a.i32(3).i32(2).sub(0x91, 0x1F);
    a.i32(0x100).i32(3).sub(0x92, 0x16);
    a.i32(2).i32(4).sub(0x91, 0x1F);
    a.i32(0x108).i32(2).sub(0x92, 0x16).ret();
    MemProvider provider; provider.set(a);
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    auto thread = vm.spawn(config, "test", "image anchors"); REQUIRE(thread);
    REQUIRE(vm.run_frame(0));
    Ctx c(vm, **thread);
    auto pixels = d.read_slot(1); REQUIRE(pixels);
    CHECK(((*pixels)[0] & 0x00FFFFFFu) == 0x00123456u);
    CHECK(c.read(0x100, 4, false) == 370);
    CHECK(c.read(0x104, 4, false) == 236);
    CHECK(c.read(0x108, 4, false) == 0xffffffffu);
    CHECK(c.read(0x10c, 4, false) == 0xffffffffu);
}

TEST_CASE("discrete slider values map to positions and dragging returns step indices" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 320, 80); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 43, 43, SlotFormat::Bgra));
    constexpr Word thumb = 0x80000005, knob = 0xF0000000;
    d.sprite(thumb).slot = 1; d.sprite(thumb).visible = true;
    SystemState state; state.display = &d;
    SubopRegistry registry; register_core_subops(registry); register_knob_subops(registry, state);
    Asm a;
    a.i32(thumb).sub(0x90, 0xD0);
    a.i32(knob).i32(216).i32(43).sub(0x90, 0xD9);
    a.i32(knob).i32(174).i32(0).sub(0x90, 0xD8);
    a.i32(knob).i32(173).i32(0).sub(0x90, 0xD6).ret();
    MemProvider provider; provider.set(a);
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    REQUIRE(vm.spawn(config, "test", "slider")); REQUIRE(vm.run_frame(0));
    CHECK(d.find_sprite(thumb)->x == 173);
    CHECK(state.knobs.at(knob).value_x == 173);
    state.knob_focus = knob; state.left_down = true; state.mouse_x = 90; state.mouse_y = 0;
    knob_input(state);
    CHECK(state.knobs.at(knob).value_x == 90);
    CHECK(d.find_sprite(thumb)->x == 90);
}

TEST_CASE("object position queries follow a moved master before rendering" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 320, 80); REQUIRE(display);
    auto& d = **display;
    constexpr Word master = 0xF0000000u, thumb = 0x80000005u;
    d.sprite(master);
    auto& child = d.sprite(thumb);
    child.has_master = true; child.master = master; child.master_dx = 27; child.master_dy = 0;
    GraphicsHost host{&d, nullptr};
    SubopRegistry registry; register_core_subops(registry); register_graphics_subops(registry, host);
    Asm a;
    a.i32(master).i32(271).i32(241).sub(0x90, 0x33);
    a.i32(0x100).i32(thumb).i32(0).sub(0x91, 0x38).ret();
    MemProvider provider; provider.set(a);
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    auto thread = vm.spawn(config, "test", "position"); REQUIRE(thread);
    REQUIRE(vm.run_frame(0));
    Ctx c(vm, **thread);
    CHECK(c.read(0x100, 4, false) == 298);
    CHECK(c.read(0x104, 4, false) == 241);
}

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

TEST_CASE("stopping menu input preserves its picture across controller restarts" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 3, 2, SlotFormat::Bgra));
    REQUIRE(d.fill_slot(1, 0xff1020f0));
    constexpr Word window = 0xB0000000u;
    auto& win = d.sprite(window); win.slot = 0xffffffffu;
    SystemState state; state.display = &d;
    SubopRegistry registry; MemProvider provider; provider.set(Asm().ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    auto thread = vm.spawn(config, "test", "menu"); REQUIRE(thread);
    Ctx c(vm, **thread);
    c.write(0x200, 1, 4); c.write(0x204, 0x300, 4);
    c.write(0x300, 1, 4); c.write(0x304, 1, 4); c.write(0x308, 0x400, 4);
    c.write(0x30c, 0xffffffffu, 4);
    c.write(0x400, 1, 4); c.write(0x404, 1, 4); c.write(0x408, 4, 4); c.write(0x40c, 5, 4);
    c.write(0x420, 1, 4); c.write(0x424, 0xffffffffu, 4); c.write(0x428, 0xffffffffu, 4);
    c.write(0x42c, 0xffffffffu, 4); c.write(0x430, 0xffffffffu, 4);
    REQUIRE(c.ok());
    std::vector<std::uint32_t> expected;
    for (Word id = 1; id <= 6; ++id) {
        auto& control = state.icon_controls[id]; control.window = window;
        REQUIRE(panel_start(state, id, c, 0x200) == 0);
        const auto button = control.groups[0].buttons[0].sprite;
        REQUIRE(d.compose()); auto before = d.read_screen(); REQUIRE(before);
        CHECK(((*before)[5 * 32 + 4] & 0xffffffu) == 0x0f1fee); // native 255/256 alpha
        if (expected.empty()) expected = *before;
        CHECK(*before == expected);
        panel_stop(state, id, true);
        state.icon_controls.erase(id);
        REQUIRE(state.icon_controls.size() == 1);
        REQUIRE(state.icon_controls.at(window).paint_only);
        REQUIRE(d.find_sprite(button));
        // A scheduler frame may occur before the replacement controller starts.
        state.press_input(1); state.mouse_x = 4; state.mouse_y = 5;
        panel_input(state); panel_sync(state);
        CHECK(state.icon_controls.at(window).events.empty());
        REQUIRE(d.compose()); auto after = d.read_screen(); REQUIRE(after);
        CHECK(*after == expected);
        win.visible = false; panel_sync(state);
        CHECK_FALSE(d.find_sprite(button)->is_shown());
        win.visible = true; panel_sync(state);
        CHECK(d.find_sprite(button)->is_shown());
        CHECK(d.sprites().size() == 2); // one window and one retained button
    }
    // An explicit new paint replaces the retained picture and owns its cleanup.
    c.write(0x404, 0, 4);
    REQUIRE(panel_paint(state, c, window, 0x200, false) == 0);
    CHECK(d.sprites().size() == 1);
    panel_stop(state, window);
    state.icon_controls.erase(window);
    CHECK(state.icon_controls.empty());
}

TEST_CASE("new panel buttons inherit hidden window state" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 2, 2, SlotFormat::Bgra));
    constexpr Word window = 0xB0000000u;
    auto& win = d.sprite(window); win.visible = true; win.fade = 0x100;
    SystemState state; state.display = &d;
    SubopRegistry registry; MemProvider provider; provider.set(Asm().ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    auto thread = vm.spawn(config, "test", "paint"); REQUIRE(thread);
    Ctx c(vm, **thread);
    c.write(0x200, 1, 4); c.write(0x204, 0x300, 4);
    c.write(0x300, 1, 4); c.write(0x304, 1, 4); c.write(0x308, 0x400, 4);
    c.write(0x30C, 0xFFFFFFFFu, 4);
    c.write(0x400, 1, 4); c.write(0x404, 1, 4); c.write(0x420, 1, 4);
    REQUIRE(c.ok());
    REQUIRE(panel_paint(state, c, window, 0x200, false) == 0);
    const auto sprite = state.icon_controls.at(window).groups[0].buttons[0].sprite;
    REQUIRE(sprite != 0); REQUIRE(d.find_sprite(sprite));
    CHECK_FALSE(d.find_sprite(sprite)->is_shown());
    win.fade = 0; panel_sync(state);
    CHECK(d.find_sprite(sprite)->is_shown());
    win.fade = 0x100; panel_sync(state);
    CHECK_FALSE(d.find_sprite(sprite)->is_shown());
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

TEST_CASE("confirming a modal does not advance the message behind it" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    REQUIRE(d.create_slot(1, 4, 4, SlotFormat::Bgra));
    constexpr Word window = 0xB0000000u, button = 0x7D000001u;
    auto& win = d.sprite(window); win.visible = true;
    auto& sp = d.sprite(button); sp.slot = 1; sp.x = 4; sp.y = 5; sp.visible = true;
    SystemState s; s.display = &d; s.message_wait_clicks = 1;
    auto& panel = s.icon_controls[1]; panel.window = window; panel.modal = true;
    panel.groups.emplace_back(); panel.groups.back().buttons.resize(2);
    auto& b = panel.groups.back().buttons[1]; b.enabled = b.shown = true; b.sprite = button; b.bmp_normal = 1;
    MessageInput message(s);
    s.mouse_x = 5; s.mouse_y = 6; s.press_input(1);
    panel_input(s);
    CHECK(panel.info[2] == 1);
    CHECK(message.poll(s) == 0);
    s.press_input(1);
    CHECK(message.poll(s) == 1);
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

TEST_CASE("92:91 draws styled window text immediately without a result or input wait" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 160, 80); REQUIRE(display);
    auto library = FontLibrary::create(); REQUIRE(library);
    REQUIRE((*library)->open("", 24));
    auto& d = **display;
    SystemState state; state.display = &d; state.code_page = 65001;
    state.planes[0] = SystemState::Plane{};
    auto& plane = *state.planes[0];
    plane.width = 160; plane.height = 80; plane.area = {0, 0, 159, 79};
    REQUIRE(d.create_slot(0x10000, 160, 80, SlotFormat::Bgra));
    TextHost text{&d, library->get()}; FontHost fonts;
    SubopRegistry registry; register_core_subops(registry);
    register_message_subops(registry, state, text, fonts);
    auto draw = [](Asm& a, Word address) {
        a.i32(0xB0000000).i32(address).i32(0xFF0000).i32(0).i32(0).i32(0);
        a.i32(1).i32(20).i32(20).i32(0x00FF00).i32(256).sub(0x92, 0x91);
    };
    MemProvider provider; Asm a;
    a.i32(0x300);
    draw(a, 0x100);
    // A sentinel below the call must survive, and the next instruction executes this frame.
    a.i32(7).raw({0x09, 0x02}).ret(); provider.set(a);
    Vm vm(provider, registry); put_string(vm, 0x100, "AB");
    ThreadConfig config; config.stack_size = 1024;
    REQUIRE(vm.spawn(config, "test", "immediate text"));
    auto status = vm.run_frame(0); REQUIRE_MESSAGE(status.has_value(), (status ? "" : status.error().message));
    std::uint8_t value[4]; REQUIRE(vm.global.read(0x300, 4, value));
    CHECK(load_u32le(value) == 7);
    CHECK(state.message_wait_clicks == 0);
    CHECK(text.messages->cursors[0].x == 24);
    auto pixels = d.read_slot(0x20000); REQUIRE(pixels);
    bool first = false, second = false, shadow = false;
    for (unsigned y = 0; y < 80; ++y) for (unsigned x = 0; x < 160; ++x) {
        const auto pixel = (*pixels)[y * 160 + x];
        if (pixel >> 24) {
            if (pixel & 0xFF0000) { if (x < 12) first = true; else second = true; }
            if (pixel & 0x00FF00) shadow = true;
        }
    }
    CHECK(first); CHECK(second); CHECK(shadow);
    // The immediate variant clears control pages synchronously, without a fade wait.
    Asm clear; draw(clear, 0x100); clear.ret(); provider.set(clear);
    Vm next(provider, registry); put_string(next, 0x100, "\x0C" "A");
    REQUIRE(next.spawn(config, "test", "clear page")); REQUIRE(next.run_frame(0));
    CHECK(text.messages->cursors[0].x == 12);
    auto cleared = d.read_slot(0x20000); REQUIRE(cleared);
    bool old_text_remains = false;
    for (unsigned y = 0; y < 80; ++y) for (unsigned x = 32; x < 160; ++x)
        old_text_remains |= (*cleared)[y * 160 + x] != 0;
    CHECK_FALSE(old_text_remains);
}

TEST_CASE("cropping a projected sprite preserves its position when its origin changes" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 32, 32); REQUIRE(display);
    auto& d = **display;
    std::vector<std::uint32_t> pixels(64, 0);
    for (int y = 1; y < 8; ++y) for (int x = 2; x < 8; ++x) pixels[y * 8 + x] = 0xff6495edu;
    REQUIRE(d.upload_slot(1, 8, 8, SlotFormat::Bgra, pixels));
    constexpr Word object = 0x80000003;
    auto& sp = d.sprite(object); sp.projected = true; sp.visible = true; sp.slot = 1;
    sp.origin_x = 4; sp.origin_y = 4; sp.blend = 1;
    REQUIRE(d.present()); REQUIRE(d.snapshot(9));
    auto before = d.read_slot(9); REQUIRE(before);

    GraphicsHost host{&d, nullptr}; SubopRegistry registry;
    register_core_subops(registry); register_graphics_subops(registry, host);
    Asm code;
    code.i32(2).i32(1).i32(2).i32(1).i32(6).i32(7).sub(0x90, 0x1F);
    code.i32(object).i32(0x40).i32(2).i32(3).sub(0x90, 0x38);
    code.i32(object).i32(2).sub(0x90, 0x57).ret();
    MemProvider provider; provider.set(code);
    Vm vm(provider, registry); ThreadConfig cfg; cfg.stack_size = 1024;
    REQUIRE(vm.spawn(cfg, "test", "crop origin")); REQUIRE(vm.run_frame(0));
    CHECK(sp.origin_x == 2); CHECK(sp.origin_y == 3);
    REQUIRE(d.present()); REQUIRE(d.snapshot(10));
    auto after = d.read_slot(10); REQUIRE(after);
    CHECK(*after == *before);
}

TEST_CASE("90:29 keeps fractional path coordinates through a small character movement" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 16, 16); REQUIRE(display);
    auto& d = **display;
    constexpr Word object = 0x80000004;
    auto& sp = d.sprite(object); sp.projected = true; sp.smooth = true;
    sp.script_fixed_move_to(0x1680000, 0);
    SystemState state; state.display = &d;
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    MemProvider provider;
    // Actual game's two points: y=2 and y=5, curve 2, duration 200 ms.
    provider.set(Asm().i32(object).i32(2).i32(0x100).i32(2).i32(0).i32(0).i32(256).i32(200).i32(250).i32(0).i32(0).i32(0x710).sub(0x90, 0x29).ret());
    Vm vm(provider, registry);
    std::uint8_t points[32]{};
    const auto put = [&](int offset, Word value) { for (int i = 0; i < 4; ++i) points[offset + i] = static_cast<std::uint8_t>(value >> (8 * i)); };
    put(0, 0x1680000); put(4, 0x20000);
    put(16, 0x1680000); put(20, 0x50000);
    REQUIRE(vm.global.write(0x100, sizeof points, points));
    ThreadConfig config; config.stack_size = 1024;
    REQUIRE(vm.spawn(config, "test", "character bob")); REQUIRE(vm.run_frame(0));
    advance_motions(state, d, 16);
    CHECK(sp.x == 360); CHECK(sp.y == 0);
    CHECK(sp.fixed_y() == 29003); // native amachoco3.exe spline at 16 ms
    CHECK(sp.fraction_y > 0); CHECK(sp.fraction_y < 65536);
    auto previous = sp.fixed_y();
    for (int ms = 32; ms <= 192; ms += 16) {
        advance_motions(state, d, ms);
        CHECK(sp.fixed_y() > previous);
        previous = sp.fixed_y();
    }
    advance_motions(state, d, 200);
    CHECK(sp.fixed_y() == 0x50000); CHECK(sp.fraction_y == 0);
    // Negative fractional positions remain negative fractions; integer moves reset the remainder.
    sp.script_fixed_move_to(-32768, -16384);
    CHECK(sp.x == -1); CHECK(sp.fraction_x == 32768);
    CHECK(sp.script_fixed_y() == -16384);
    sp.script_move_to(1, 2); CHECK(sp.fraction_x == 0); CHECK(sp.fraction_y == 0);
}

TEST_CASE("90:28 stops a long motion when a forced notification arrives" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 16, 16); REQUIRE(display);
    auto& d = **display;
    constexpr Word object = 0x80000004;
    auto& sp = d.sprite(object); sp.projected = true; sp.visible = true;
    SystemState state; state.display = &d;
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    MemProvider provider;
    provider.set(Asm().i32(object).i32(100 << 16).i32(50 << 16).i32(0).i32(128).i32(0).i32(256)
                     .i32(60000).i32(250).i32(0).i32(0).i32(0x710).sub(0x90, 0x28).ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    auto thread = vm.spawn(config, "test", "motion"); REQUIRE(thread);
    const Word id = (*thread)->id;
    std::vector<Word> result;
    vm.trace_completion = [&](const Thread&, std::uint16_t op, const std::vector<Word>& values) {
        if (op == 0x9028) result = values;
    };
    REQUIRE(vm.run_frame(0)); REQUIRE(state.motions.size() == 1);
    advance_motions(state, d, 30000);
    CHECK(sp.script_x() == 50); CHECK(sp.script_y() == 25);
    Thread sender; sender.expr.resize(16); Ctx ctx(vm, sender);
    const auto* notify = registry.find(0x804c); REQUIRE(notify);
    ctx.push(id); ctx.push(1); ctx.push(0); ctx.push(0); notify->fn(ctx);
    REQUIRE(vm.run_frame(10)); CHECK(result.empty()); CHECK(state.motions.size() == 1);
    ctx.push(id); ctx.push(1); ctx.push(1); ctx.push(0); notify->fn(ctx);
    REQUIRE(vm.run_frame(20));
    REQUIRE(result.size() == 2); CHECK(result[1] == 1);
    CHECK(state.motions.empty()); CHECK(sp.script_x() == 100); CHECK(sp.script_y() == 50);
    CHECK(sp.level() == 128);
}

TEST_CASE("90:28 cancellation releases the thread without completing the motion" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 16, 16); REQUIRE(display);
    auto& d = **display;
    constexpr Word object = 0x80000004;
    auto& sp = d.sprite(object); sp.projected = true; sp.visible = true;
    SystemState state; state.display = &d;
    SubopRegistry registry; register_core_subops(registry); register_plane_subops(registry, state, &d);
    MemProvider provider;
    provider.set(Asm().i32(object).i32(100 << 16).i32(50 << 16).i32(0).i32(128).i32(0).i32(256)
                     .i32(60000).i32(250).i32(0).i32(0).i32(0x710).sub(0x90, 0x28).ret());
    Vm vm(provider, registry); ThreadConfig config; config.stack_size = 1024;
    auto thread = vm.spawn(config, "test", "motion"); REQUIRE(thread);
    std::vector<Word> result;
    vm.trace_completion = [&](const Thread&, std::uint16_t op, const std::vector<Word>& values) {
        if (op == 0x9028) result = values;
    };
    REQUIRE(vm.run_frame(0)); REQUIRE(state.motions.size() == 1);
    Thread sender; sender.expr.resize(16); Ctx ctx(vm, sender);
    const auto* notify = registry.find(0x804c); REQUIRE(notify);
    ctx.push((*thread)->id); ctx.push(0); ctx.push(0); ctx.push(0); notify->fn(ctx);
    REQUIRE(vm.run_frame(20));
    REQUIRE(result.size() == 2); CHECK(result[1] == 0xffffffffu);
    CHECK(state.motions.empty()); CHECK(sp.script_x() == 0); CHECK(sp.script_y() == 0);
}

TEST_CASE("recreating a window resets its text cursor" * doctest::test_suite("gpu")) {
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 16, 16); REQUIRE(display);
    SystemState state; TextHost text; FontHost fonts;
    SubopRegistry registry; register_core_subops(registry);
    register_plane_subops(registry, state, display->get());
    register_message_subops(registry, state, text, fonts);
    MemProvider provider; Vm vm(provider, registry);
    Thread thread; thread.expr.resize(32); Ctx c(vm, thread);
    const auto call = [&](Word op) { const auto* def = registry.find(op); REQUIRE(def); def->fn(c); REQUIRE(c.ok()); };
    c.push(100); c.push(40); call(0x9080); const Word handle = c.pop();
    c.push(handle); c.push(268); c.push(0); call(0x918c);
    c.push(handle); call(0x9081);
    c.push(100); c.push(40); call(0x9080); CHECK(c.pop() == handle);
    c.push(handle); call(0x918d);
    const Word y = c.pop(), x = c.pop(), ok = c.pop();
    CHECK(ok == 1); CHECK(x == 0); CHECK(y == 0);
}
