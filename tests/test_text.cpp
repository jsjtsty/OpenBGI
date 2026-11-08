#include <doctest/doctest.h>

#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "bindings/graphics_subops.hpp"
#include "bindings/text_subops.hpp"
#include "support.hpp"
#include "text/encoding.hpp"
#include "text/layout.hpp"
#include "vm/vm.hpp"

using namespace obgi;

TEST_CASE("cp932 decoding") {
    CHECK(cp932_to_utf32("ABC") == U"ABC");
    CHECK(cp932_to_utf32("\x82\xA0\x82\xA2") == U"あい");           // あい
    CHECK(cp932_to_utf32("\x83\x41") == U"ア");                         // ア
    CHECK(cp932_to_utf32("\x81\x40") == U"　");                         // ideographic space
    CHECK(cp932_to_utf32("\xB1\xB2") == U"ｱｲ");                   // half-width katakana
    CHECK(cp932_to_utf32("\x88\x9F") == U"亜");                         // 亜
    CHECK(cp932_to_utf32("\x82") == U"�");                             // truncated pair
    CHECK(cp932_to_utf32("\x81\x7F") == U"�");                         // invalid trail
    CHECK(utf8_to_utf32("a\xE3\x81\x82\xF0\x9F\x98\x80") == U"aあ\U0001F600");
    CHECK(utf8_to_utf32("\xE3\x81") == U"��");                    // truncated sequence
}

namespace {

std::shared_ptr<Font> open_font(FontLibrary& lib, int size) {
    auto f = lib.open("MS Gothic", size);
    if (!f) {
        MESSAGE("no Japanese font installed: ", f.error().message);
        return nullptr;
    }
    return *f;
}

} // namespace

TEST_CASE("simplified Chinese font is preferred over script Japanese font" * doctest::test_suite("host")) {
    if (std::getenv("OBGI_FONT")) return; // An explicit override takes precedence.
    const auto dir = std::filesystem::temp_directory_path() / "obgi_font_preference_test";
    std::filesystem::create_directories(dir);
    std::ofstream(dir / "msyh.ttc").put('x');
    std::ofstream(dir / "msgothic.ttc").put('x');
    auto library = FontLibrary::create(); REQUIRE(library);
    (*library)->add_search_dir(dir);
    auto resolved = (*library)->resolve("MS Gothic"); REQUIRE(resolved);
#ifdef __APPLE__
    CHECK((resolved->filename() == "PingFang.ttc" || resolved->filename() == "msyh.ttc"));
#else
    CHECK(resolved->filename() == "msyh.ttc");
#endif
    CHECK((*library)->has_exact("MS Gothic"));
    std::filesystem::remove_all(dir);
}

TEST_CASE("macOS system Chinese fonts cover simplified text" * doctest::test_suite("host")) {
#ifdef __APPLE__
    if (std::getenv("OBGI_FONT")) return;
    auto library = FontLibrary::create(); REQUIRE(library);
    auto font = (*library)->open("MS Gothic", 24); REQUIRE(font);
    for (char32_t ch : std::u32string_view(U"简体中文游戏更改语言选择あア"))
        CHECK((*font)->glyph(ch) != nullptr);
#endif
}

TEST_CASE("embedded fonts and their glyphs outlive the registering library" * doctest::test_suite("host")) {
    auto library = FontLibrary::create(); REQUIRE(library);
    auto source = (*library)->resolve("MS Gothic"); REQUIRE(source);
    std::ifstream file(*source, std::ios::binary);
    REQUIRE(file.good());
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
    REQUIRE((*library)->register_file("embedded-font", std::move(bytes)));
    CHECK((*library)->has_exact("embedded-font"));
    auto font = (*library)->open("embedded-font", 24); REQUIRE(font);
    auto reused = (*library)->open("embedded-font", 24); REQUIRE(reused);
    CHECK(font->get() == reused->get());
    CHECK((*font)->glyph(U'\U0010FFFF') == nullptr);
    library->reset();
    REQUIRE((*font)->glyph(U'A'));
    CHECK((*font)->glyph(U'A')->advance > 0);
}

TEST_CASE("mapped and lazy fallback fonts survive their library" * doctest::test_suite("host")) {
#ifdef _WIN32
    const std::filesystem::path latin = "C:/Windows/Fonts/arial.ttf";
    const std::filesystem::path chinese = "C:/Windows/Fonts/msyh.ttc";
    if (!std::filesystem::exists(latin) || !std::filesystem::exists(chinese)) return;
    auto library = FontLibrary::create(); REQUIRE(library);
    auto mapped = (*library)->open("MS Gothic", 24); REQUIRE(mapped);
    std::ifstream file(latin, std::ios::binary);
    REQUIRE(file.good());
    std::vector<std::uint8_t> bytes(std::istreambuf_iterator<char>(file), {});
    REQUIRE((*library)->register_file("latin-test", std::move(bytes)));
    auto font = (*library)->open("latin-test", 24); REQUIRE(font);
    REQUIRE((*font)->glyph(U'A'));
    library->reset();
    REQUIRE((*mapped)->glyph(U'中'));
    const Glyph* fallback = (*font)->glyph(U'中');
    REQUIRE(fallback);
    CHECK(fallback->width > 0);
    CHECK((*font)->glyph(U'中') == fallback);
#endif
}

TEST_CASE("text layout with a system font" * doctest::test_suite("host")) {
    auto lib = FontLibrary::create();
    REQUIRE(lib.has_value());
    auto font = open_font(**lib, 24);
    REQUIRE(font);
    CHECK(font->line_height() >= 24);

    auto plain = render_text(*font, U"あいう", {}, 0);
    REQUIRE(plain.has_value());
    CHECK(plain->lines == 1);
    CHECK(plain->width >= 3 * 20);
    std::size_t ink = 0;
    for (std::uint32_t px : plain->pixels) ink += (px >> 24) != 0;
    CHECK(ink > 100);

    TextStyle red;
    red.color = 0xFF0000;
    auto tinted = render_text(*font, U"あ", red, 0);
    REQUIRE(tinted.has_value());
    bool saw_full = false;
    for (std::uint32_t px : tinted->pixels)
        if ((px >> 24) == 255) {
            CHECK((px & 0xFFFFFF) == 0xFF0000u); // BGRA with colour 0xRRGGBB red
            saw_full = true;
        }
    CHECK(saw_full);

    auto two = render_text(*font, U"あ\nい", {}, 0);
    REQUIRE(two.has_value());
    CHECK(two->lines == 2);
    CHECK(two->height >= 2u * font->line_height());

    auto wrapped = render_text(*font, U"あいうえお", {}, 50);
    REQUIRE(wrapped.has_value());
    CHECK(wrapped->lines >= 2);
    CHECK(wrapped->width <= 50);
}

TEST_CASE("dialogue layout uses script spacing and continuation indentation" * doctest::test_suite("host")) {
    auto lib = FontLibrary::create(); REQUIRE(lib);
    auto font = open_font(**lib, 28); REQUIRE(font);
    TextStyle style;
    style.fixed_advance = 28;
    style.row_height = 43;
    style.line_gap = 15;
    style.first_indent = 16;
    style.continuation_indent = 44;
    style.right_margin = 28;
    auto layout = layout_text(*font, U"「你好世界」", style, 130);
    REQUIRE(layout.glyphs.size() == 6);
    CHECK(layout.row_height == 43);
    CHECK(layout.glyphs[0].x == 16);
    CHECK(layout.glyphs[1].x == 44);
    CHECK(layout.glyphs[3].line == 1);
    CHECK(layout.glyphs[3].x == 44);
    for (const auto& glyph : layout.glyphs)
        CHECK(glyph.line * layout.row_height + layout.ascent - glyph.glyph.top + glyph.glyph.height <= static_cast<int>(layout.height));
}

namespace {

struct Asm {
    std::vector<std::uint8_t> b;
    Asm& raw(std::initializer_list<int> bytes) {
        for (int x : bytes) b.push_back(static_cast<std::uint8_t>(x));
        return *this;
    }
    Asm& i32(vm::Word v) { return raw({0x02, int(v & 255), int((v >> 8) & 255), int((v >> 16) & 255), int(v >> 24)}); }
    Asm& sub(int op, int sub) { return raw({op, sub}); }
    Asm& ret() { return raw({0x17}); }
};

class OneProgram final : public vm::ProgramProvider {
  public:
    std::vector<std::uint8_t> program;
    Result<std::vector<std::uint8_t>> load(std::string_view, std::string_view) override { return program; }
};

} // namespace

TEST_CASE("92:9C draws text into an image slot" * doctest::test_suite("gpu")) {
    auto fonts = FontLibrary::create();
    REQUIRE(fonts.has_value());
    if (!open_font(**fonts, 24)) return;

    RendererOptions ro;
    ro.headless = true;
    auto renderer = Renderer::create(ro);
    REQUIRE(renderer);
    auto display = Display::create(**renderer);
    REQUIRE(display.has_value());
    TextHost host{display->get(), fonts->get()};
    vm::SubopRegistry registry;
    vm::register_core_subops(registry);
    register_text_subops(registry, host);
    // the graphics ops are needed to create the target slot
    ArcResourceFinder finder(obgi_test::game_dir());
    ArcImageSource images(finder);
    GraphicsHost gh{display->get(), &images};
    register_graphics_subops(registry, gh);

    OneProgram provider;
    vm::Vm vm(provider, registry);
    const char* name = "MS Gothic";
    REQUIRE(vm.global.write(0x100, std::strlen(name) + 1, reinterpret_cast<const std::uint8_t*>(name)));
    const char text[] = "\x82\xA0\x82\xA2\x82\xA4"; // あいう
    REQUIRE(vm.global.write(0x200, sizeof text, reinterpret_cast<const std::uint8_t*>(text)));
    const vm::Word result_addr = 0x300;

    Asm a;
    a.i32(result_addr);
    a.i32(5).i32(200).i32(60).i32(1).sub(0x90, 0x11); // slot 5: 200x60 BGRX
    a.i32(5).i32(0).sub(0x90, 0x13);
    // slot x y text color ruby_on ruby_dict ruby_param font size p2 p3 vertical kinsoku gap mode a b ecolor ealpha reserved
    a.i32(5).i32(10).i32(8).i32(0x200).i32(0xFFFFFF).i32(0).i32(0).i32(0).i32(0x100).i32(24).i32(100).i32(0);
    a.i32(0).i32(0).i32(0).i32(0).i32(0).i32(0).i32(0).i32(0).i32(0);
    a.sub(0x92, 0x9C);
    a.raw({0x09, 0x02}).ret(); // [result_addr] = lines
    provider.program = {8, 0, 0, 0};
    for (int i = 0; i < 4; ++i) provider.program.push_back(static_cast<std::uint8_t>(a.b.size() >> (8 * i)));
    provider.program.insert(provider.program.end(), a.b.begin(), a.b.end());

    vm::ThreadConfig cfg;
    cfg.stack_size = 0x1000;
    cfg.program_capacity = 0x10000;
    REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
    auto st = vm.run_frame(0);
    REQUIRE_MESSAGE(st.has_value(), std::string(st ? "" : st.error().message));

    std::uint8_t out[4];
    REQUIRE(vm.global.read(result_addr, 4, out));
    CHECK(load_u32le(out) == 1);
    auto px = (*display)->read_slot(5);
    REQUIRE(px.has_value());
    std::size_t lit = 0;
    for (std::uint32_t p : *px) lit += (p & 0xFFFFFF) != 0;
    CHECK(lit > 100);
}
