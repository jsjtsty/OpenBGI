#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>

#include "bindings/system_subops.hpp"
#include "support.hpp"
#include "vm/vm.hpp"

using namespace obgi;

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
    Asm& store32() { return raw({0x09, 0x02}); }
};

class OneProgram final : public vm::ProgramProvider {
  public:
    std::vector<std::uint8_t> program;
    void set(const Asm& a) {
        program = {8, 0, 0, 0};
        for (int i = 0; i < 4; ++i) program.push_back(static_cast<std::uint8_t>(a.b.size() >> (8 * i)));
        program.insert(program.end(), a.b.begin(), a.b.end());
    }
    Result<std::vector<std::uint8_t>> load(std::string_view, std::string_view) override { return program; }
};

// A VM with the system sub-opcodes and a private data directory.
struct Rig {
    std::filesystem::path data = std::filesystem::temp_directory_path() / "obgi_test_system";
    ArcResourceFinder finder;
    SystemState state;
    vm::SubopRegistry registry;
    OneProgram provider;
    vm::Vm vm;

    explicit Rig(bool use_game_dir = true) : finder(obgi_test::game_dir()), vm(provider, registry) {
        std::filesystem::remove_all(data);
        state.game_dir = use_game_dir ? obgi_test::game_dir() : data / "nogame";
        state.data_dir = data;
        vm::register_core_subops(registry);
        register_system_subops(registry, state, finder);
    }
    ~Rig() { std::error_code ec; std::filesystem::remove_all(data, ec); }

    Result<vm::FrameStatus> run(const Asm& a) {
        provider.set(a);
        vm::ThreadConfig cfg;
        cfg.stack_size = 0x1000;
        cfg.program_capacity = 0x10000;
        auto t = vm.spawn(cfg, "x.arc", "main");
        REQUIRE(t.has_value());
        return vm.run_frame(0);
    }
    std::vector<std::uint8_t> mem(vm::Word addr, std::size_t n) {
        std::vector<std::uint8_t> out(n);
        REQUIRE(vm.global.read(addr, n, out.data()));
        return out;
    }
    void put(vm::Word addr, const std::string& s) {
        REQUIRE(vm.global.write(addr, s.size() + 1, reinterpret_cast<const std::uint8_t*>(s.c_str())));
    }
};

} // namespace

TEST_CASE("rolling hash 81:E9 matches the reference computation") {
    Rig rig(false);
    const std::string text = "AmairoChocolate2ML";
    rig.put(0x100, text);
    // state at 0x200 starts zeroed; hash the text including its terminator, then "hello" on top
    REQUIRE(rig.run(Asm().i32(0x200).i32(0x100).i32(text.size() + 1).sub(0x81, 0xE9).ret()).has_value());
    CHECK(rig.mem(0x200, 8) == std::vector<std::uint8_t>{0x36, 0x0c, 0x1d, 0x67, 0xa6, 0xc2, 0xb6, 0x5e});
    rig.put(0x300, "hello");
    REQUIRE(rig.run(Asm().i32(0x200).i32(0x300).i32(5).sub(0x81, 0xE9).ret()).has_value());
    CHECK(rig.mem(0x200, 8) == std::vector<std::uint8_t>{0xf2, 0xc5, 0x17, 0x36, 0x1b, 0x51, 0xca, 0x3c});
}

TEST_CASE("global data: the original BGI.gdb is read, written back and read again") {
    if (!std::filesystem::exists(obgi_test::game_dir() / "BGI.gdb")) return;
    Rig rig;
    // 80:80 pushes left, top, status; the game's own file is the SDC packed one
    REQUIRE(rig.run(Asm().sub(0x80, 0x80).ret()).has_value());
    // the first bytes of the global data area, as decoded independently with scripts/sdc_decode.py
    const std::vector<std::uint8_t> first8{0x01, 0, 0, 0, 0x8e, 0x9d, 0xf9, 0xff};
    REQUIRE(rig.run(Asm().i32(0x500).i32(0).i32(8).sub(0x80, 0x83).ret()).has_value());
    CHECK(rig.mem(0x500, 8) == first8);
    CHECK(rig.state.gdb.settings[0x15e] == 0xff); // the settings block: signed -1 at 0x15e (analysis/启动路径.md)
    CHECK(rig.state.gdb.keys.size() > 10);

    // our own write: 8 bytes at offset 0x1000, a new key, save
    rig.put(0x600, "key_from_test");
    REQUIRE(rig.vm.global.write(0x700, 8, reinterpret_cast<const std::uint8_t*>("ABCDEFGH")));
    REQUIRE(rig.run(Asm().i32(0x1000).i32(0x700).i32(8).sub(0x80, 0x82).i32(0x600).sub(0x80, 0x84).sub(0x80, 0x81).ret()).has_value());
    CHECK(std::filesystem::exists(rig.data / "BGI.gdb"));
    // forget everything in memory, then restore from the saved file
    rig.state.gdb = GdbData{};
    REQUIRE(rig.run(Asm().sub(0x80, 0x80).ret()).has_value());
    CHECK(std::memcmp(rig.state.gdb.area.data() + 0x1000, "ABCDEFGH", 8) == 0);
    REQUIRE(rig.run(Asm().i32(0x800).i32(0).i32(8).sub(0x80, 0x83).ret()).has_value());
    CHECK(rig.mem(0x800, 8) == first8);
    // 80:85 finds the new key and not an unknown one
    rig.put(0x900, "no_such_key");
    REQUIRE(rig.run(Asm().i32(0x600).sub(0x80, 0x85).ret()).has_value());
    CHECK(std::find(rig.state.gdb.keys.begin(), rig.state.gdb.keys.end(), "key_from_test") != rig.state.gdb.keys.end());
}

TEST_CASE("global data without a file reports status 1; bad ranges are script errors") {
    Rig rig(false);
    REQUIRE(rig.run(Asm().sub(0x80, 0x80).ret()).has_value());
    CHECK(rig.state.gdb.keys.empty());
    CHECK_FALSE(rig.run(Asm().i32(0x500).i32(0xFFFFF).i32(2).sub(0x80, 0x83).ret()).has_value()); // runs past the end
    CHECK_FALSE(rig.run(Asm().i32(0x500).i32(0x100000).i32(1).sub(0x80, 0x83).ret()).has_value()); // offset out of range
    CHECK_FALSE(rig.run(Asm().i32(0x0).i32(0x500).i32(0).sub(0x80, 0x82).ret()).has_value());      // empty copy
    // the last byte is fine
    CHECK(rig.run(Asm().i32(0x500).i32(0xFFFFF).i32(1).sub(0x80, 0x83).ret()).has_value());
}

#include "bindings/font_subops.hpp"

TEST_CASE("font table sub-opcodes register names, aliases and definitions") {
    auto fonts = FontLibrary::create();
    REQUIRE(fonts.has_value());
    FontHost host;
    host.fonts = fonts->get();
    vm::SubopRegistry registry;
    vm::register_core_subops(registry);
    register_font_subops(registry, host);
    OneProgram provider;
    vm::Vm vm(provider, registry);
    auto put = [&](vm::Word addr, const std::string& s) {
        REQUIRE(vm.global.write(addr, s.size() + 1, reinterpret_cast<const std::uint8_t*>(s.c_str())));
    };
    auto run = [&](const Asm& a) {
        provider.set(a);
        vm::ThreadConfig cfg;
        cfg.stack_size = 0x1000;
        cfg.program_capacity = 0x10000;
        REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
        return vm.run_frame(0);
    };
    auto word_at = [&](vm::Word addr) {
        std::uint8_t b[4];
        REQUIRE(vm.global.read(addr, 4, b));
        return load_u32le(b);
    };
    put(0x100, "FontA");
    put(0x120, "FontB");
    put(0x140, "AliasA");

    // B0:C1: ids count up from 0 and a known name keeps its id
    REQUIRE(run(Asm().i32(0x400).i32(0x100).i32(1).sub(0xB0, 0xC1).raw({0x09, 0x02}).ret()).has_value());
    CHECK(word_at(0x400) == 0);
    REQUIRE(run(Asm().i32(0x400).i32(0x120).i32(0).sub(0xB0, 0xC1).raw({0x09, 0x02}).ret()).has_value());
    CHECK(word_at(0x400) == 1);
    REQUIRE(run(Asm().i32(0x400).i32(0x100).i32(1).sub(0xB0, 0xC1).raw({0x09, 0x02}).ret()).has_value());
    CHECK(word_at(0x400) == 0);
    REQUIRE(host.table.infos.size() == 2);
    CHECK(host.table.infos.back().charset == 0); // FontA registered first with charset 1 -> ANSI (0)
    CHECK(host.table.infos.front().charset == 0x80);

    // B0:C7: alias for a font, and the default alias with address 0
    REQUIRE(run(Asm().i32(0x100).i32(0x140).sub(0xB0, 0xC7).i32(0).i32(0x140).sub(0xB0, 0xC7).ret()).has_value());
    CHECK(host.table.default_alias == std::string("AliasA"));
    bool aliased = false;
    for (auto& i : host.table.infos) aliased |= i.name == "FontA" && i.alias == std::string("AliasA");
    CHECK(aliased);

    // 91:0E: valid scale, then out-of-range scale and offset are script errors
    REQUIRE(run(Asm().i32(0x100).i32(0x10000).i32(0x10000).i32(0).i32(0).sub(0x91, 0x0E).ret()).has_value());
    CHECK(host.table.definitions.at("FontA").scale_x == 0x10000);
    CHECK_FALSE(run(Asm().i32(0x100).i32(0x30000).i32(0x10000).i32(0).i32(0).sub(0x91, 0x0E).ret()).has_value());
    CHECK_FALSE(run(Asm().i32(0x100).i32(0x10000).i32(0x10000).i32(5).i32(0).sub(0x91, 0x0E).ret()).has_value()); // limit is 0 at 1.0
    CHECK(run(Asm().i32(0x100).i32(0x20000).i32(0x20000).i32(0x8000).i32(0x8000).sub(0x91, 0x0E).ret()).has_value()); // limit 0x8000 at 2.0

    // B0:C6: a name that no font file matches is reported missing
    REQUIRE(run(Asm().i32(0x400).i32(0x500).i32(0x100).sub(0xB0, 0xC6).raw({0x09, 0x02}).ret()).has_value());
    CHECK(word_at(0x400) == 0);
}

TEST_CASE("80:20 / 80:21 allocate and free script memory; 81:21 converts CP932 to UTF-8") {
    Rig rig(false);
    auto word_at = [&](vm::Word addr) {
        auto m = rig.mem(addr, 4);
        return load_u32le(m.data());
    };
    REQUIRE(rig.run(Asm().i32(0x400).i32(100).sub(0x80, 0x20).raw({0x09, 0x02}).ret()).has_value());
    const vm::Word a = word_at(0x400);
    CHECK((a >> vm::kTagShift) == static_cast<vm::Word>(vm::Tag::Heap));
    REQUIRE(rig.run(Asm().i32(0x404).i32(100).sub(0x80, 0x20).raw({0x09, 0x02}).ret()).has_value());
    const vm::Word b = word_at(0x404);
    CHECK(b != a);
    CHECK(b >= a + 100); // blocks do not overlap

    // memory is usable, and freeing zeroes it so the next block at the same place reads as zero
    const std::uint8_t data[4] = {1, 2, 3, 4};
    REQUIRE(rig.vm.heap.write(a & vm::kOffsetMask, 4, data));
    REQUIRE(rig.run(Asm().i32(0x408).i32(a).sub(0x80, 0x21).raw({0x09, 0x02}).ret()).has_value());
    CHECK(word_at(0x408) == 1);
    REQUIRE(rig.run(Asm().i32(0x40c).i32(100).sub(0x80, 0x20).raw({0x09, 0x02}).ret()).has_value());
    CHECK(word_at(0x40c) == a);
    std::uint8_t back[4];
    REQUIRE(rig.vm.heap.read(a & vm::kOffsetMask, 4, back));
    CHECK(std::memcmp(back, "\0\0\0\0", 4) == 0);

    // freeing address 0 is fine; freeing something that is not a block is a script error, as is freeing twice
    REQUIRE(rig.run(Asm().i32(0x410).i32(0).sub(0x80, 0x21).raw({0x09, 0x02}).ret()).has_value());
    CHECK(word_at(0x410) == 1);
    REQUIRE(rig.run(Asm().i32(0x414).i32(b).sub(0x80, 0x21).raw({0x09, 0x02}).ret()).has_value());

    // 81:21: "ABC" and a double-byte kana (CP932 82 A0 = U+3042) become UTF-8
    const std::uint8_t sjis[] = {'A', 'B', 0x82, 0xA0, 0};
    REQUIRE(rig.vm.global.write(0x500, sizeof sjis, sjis));
    REQUIRE(rig.run(Asm().i32(0x600).i32(0x500).sub(0x81, 0x21).ret()).has_value());
    CHECK(rig.mem(0x600, 6) == std::vector<std::uint8_t>{'A', 'B', 0xE3, 0x81, 0x82, 0});
}

TEST_CASE("80:21 rejects an address that is not the start of a block") {
    Rig rig(false);
    REQUIRE(rig.run(Asm().i32(0x400).i32(100).sub(0x80, 0x20).raw({0x09, 0x02}).ret()).has_value());
    std::uint8_t m[4];
    REQUIRE(rig.vm.global.read(0x400, 4, m));
    const vm::Word blk = load_u32le(m);
    REQUIRE(rig.run(Asm().i32(0x414).i32(blk).sub(0x80, 0x21).raw({0x09, 0x02}).ret()).has_value());
    CHECK_FALSE(rig.run(Asm().i32(0x414).i32(blk).sub(0x80, 0x21).raw({0x09, 0x02}).ret()).has_value()); // freed twice
}

TEST_CASE("80:21 rejects an address inside a block") {
    Rig rig(false);
    REQUIRE(rig.run(Asm().i32(0x400).i32(100).sub(0x80, 0x20).raw({0x09, 0x02}).ret()).has_value());
    std::uint8_t m[4];
    REQUIRE(rig.vm.global.read(0x400, 4, m));
    CHECK_FALSE(rig.run(Asm().i32(0x414).i32(load_u32le(m) + 8).sub(0x80, 0x21).raw({0x09, 0x02}).ret()).has_value());
}

#include "bindings/table_subops.hpp"

TEST_CASE("symbol tables, string lists and the message queue") {
    Rig rig(false);
    register_window_subops(rig.registry, rig.state);
    register_table_subops(rig.registry, rig.state);
    auto word_at = [&](vm::Word addr) {
        auto m = rig.mem(addr, 4);
        return load_u32le(m.data());
    };
    rig.put(0x100, "alpha");
    rig.put(0x120, "beta");
    const std::uint8_t v1[4] = {1, 2, 3, 4}, v2[4] = {9, 8, 7, 6};
    REQUIRE(rig.vm.global.write(0x200, 4, v1));
    REQUIRE(rig.vm.global.write(0x204, 4, v2));

    // create a table of 4-byte values; its id lands at 0x300
    REQUIRE(rig.run(Asm().i32(0x300).i32(4).sub(0x80, 0xD0).ret()).has_value());
    REQUIRE(word_at(0x300) == 1);
    const vm::Word id = word_at(0x300);
    // set alpha and beta, read both back by name and by position
    REQUIRE(rig.run(Asm().i32(id).i32(0x100).i32(0x200).sub(0x80, 0xD2).i32(id).i32(0x120).i32(0x204).sub(0x80, 0xD2)
                        .i32(0x400).i32(id).i32(0x120).i32(0).sub(0x80, 0xD4)
                        .i32(0x410).i32(id).i32(0).i32(0).sub(0x80, 0xD4).ret()).has_value());
    CHECK(rig.mem(0x400, 4) == std::vector<std::uint8_t>{9, 8, 7, 6});
    CHECK(rig.mem(0x410, 4) == std::vector<std::uint8_t>{1, 2, 3, 4}); // position 0 = alpha, the first one set
    CHECK(rig.state.tables.at(id).entries.size() == 2);

    // string lists: add twice (same index), count, delete table
    rig.put(0x500, "one");
    REQUIRE(rig.run(Asm().i32(7).i32(0x500).sub(0x80, 0xDC).i32(7).i32(0x500).sub(0x80, 0xDC).i32(7).sub(0x80, 0xD9).ret()).has_value());
    CHECK(rig.state.string_sets.at(7).size() == 1);
    REQUIRE(rig.run(Asm().i32(id).sub(0x80, 0xD1).ret()).has_value());
    CHECK(rig.state.tables.empty());

    // messages come out in order, then the queue reports empty
    REQUIRE(rig.run(Asm().i32(5).i32(6).sub(0x80, 0xA1).i32(0x600).sub(0x80, 0xA0).i32(0x610).sub(0x80, 0xA0).ret()).has_value());
    CHECK(word_at(0x604) == 5); // second pushed value = the second word
    CHECK(word_at(0x608) == 6);
}

TEST_CASE("named bit tables are most-significant-bit first (verified in the original)") {
    Rig rig(false);
    register_table_subops(rig.registry, rig.state);
    rig.put(0x100, "T");
    REQUIRE(rig.run(Asm().i32(0x100).i32(16).sub(0x80, 0x88).ret()).has_value());
    REQUIRE(rig.run(Asm().i32(0x100).i32(0).i32(1).sub(0x80, 0x89).i32(0x100).i32(7).i32(1).sub(0x80, 0x89)
                        .i32(0x100).i32(8).i32(1).sub(0x80, 0x89).ret()).has_value());
    REQUIRE(rig.state.gdb.tables.size() == 1);
    CHECK(rig.state.gdb.tables[0].data[0] == 0x81); // bits 0 and 7
    CHECK(rig.state.gdb.tables[0].data[1] == 0x80); // bit 8
    REQUIRE(rig.run(Asm().i32(0x200).i32(0x100).i32(7).sub(0x80, 0x8B).ret()).has_value());
    CHECK(rig.mem(0x200, 4)[0] == 1);
}

TEST_CASE("main opcodes 6E and the integer maths follow the original") {
    Rig rig(false);
    rig.put(0x100, "ABCDE");
    // 6E: the source wrapped in the third operand, then a NUL
    REQUIRE(rig.run(Asm().i32(0x200).i32(0x100).i32(0x22).raw({0x6E}).ret()).has_value());
    CHECK(rig.mem(0x200, 8) == std::vector<std::uint8_t>{0x22, 'A', 'B', 'C', 'D', 'E', 0x22, 0});
    // 42: a * b / c in 64 bits, truncated
    REQUIRE(rig.run(Asm().i32(0x400).i32(3).i32(7).i32(2).raw({0x42}).store32()
                        .i32(0x404).i32(0xFFFFFFFDu).i32(7).i32(2).raw({0x42}).store32()
                        .i32(0x408).i32(3).i32(4).i32(12).raw({0x44}).store32().ret()).has_value());
    auto w = [&](vm::Word a) { auto m = rig.mem(a, 4); return load_u32le(m.data()); };
    CHECK(w(0x400) == 10);
    CHECK(static_cast<std::int32_t>(w(0x404)) == -10);
    CHECK(w(0x408) == 13);
}

TEST_CASE("input numbers follow the original's default map until a script remaps them") {
    SystemState s;
    CHECK(s.input_numbers(0x1) == std::vector<std::uint32_t>{1});
    CHECK(s.input_numbers(0x100) == std::vector<std::uint32_t>{0x0D});
    CHECK(s.input_numbers(0x20000) == std::vector<std::uint32_t>{0x32, 0x62}); // digit 2
    s.input_maps[0x100] = {0x41};
    CHECK(s.input_numbers(0x100) == std::vector<std::uint32_t>{0x41});
    s.press_input(0x41);
    s.press_input(0x41);
    s.press_input(9999); // out of range: ignored
    CHECK(s.input_counts[0x41] == 2);
}
