#include <doctest/doctest.h>

#include <cstring>

#include "bindings/arc_program_provider.hpp"
#include "support.hpp"
#include "vm/vm.hpp"

using namespace obgi;
using namespace obgi::vm;

namespace {

// Tiny assembler for hand-written test programs.
struct Asm {
    std::vector<std::uint8_t> b;
    Word pos() const { return static_cast<Word>(b.size()); }
    Asm& raw(std::initializer_list<int> bytes) {
        for (int x : bytes) b.push_back(static_cast<std::uint8_t>(x));
        return *this;
    }
    Asm& i8(int v) { return raw({0x00, v}); }
    Asm& i32(Word v) { return raw({0x02, int(v & 255), int((v >> 8) & 255), int((v >> 16) & 255), int(v >> 24)}); }
    Asm& store32() { return raw({0x09, 0x02}); }
    Asm& load32() { return raw({0x08, 0x02}); }
    Asm& sub(int op, int sub) { return raw({op, sub}); }
    Asm& ret() { return raw({0x17}); }
    // Emits `op mode delta16` (branch family) targeting an absolute program offset.
    Asm& branch(int op, int mode, Word target) {
        std::int32_t d = static_cast<std::int32_t>(target) - static_cast<std::int32_t>(pos());
        return raw({op, mode, d & 255, (d >> 8) & 255});
    }
    Asm& set32(Word addr, Word value) { return i32(addr).i32(value).store32(); }
};

std::vector<std::uint8_t> block(const Asm& a) {
    std::vector<std::uint8_t> out = {8, 0, 0, 0};
    Word n = a.pos();
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
    out.insert(out.end(), a.b.begin(), a.b.end());
    return out;
}

class MemProvider final : public ProgramProvider {
  public:
    std::map<std::string, std::vector<std::uint8_t>> blocks;
    void put(const std::string& name, const Asm& a) { blocks[name] = block(a); }
    Result<std::vector<std::uint8_t>> load(std::string_view, std::string_view name) override {
        auto it = blocks.find(std::string(name));
        if (it == blocks.end()) return fail(Errc::Io, "missing " + std::string(name));
        return it->second;
    }
};

Word g32(Vm& vm, Word addr) {
    std::uint8_t b[4];
    REQUIRE(vm.global.read(addr, 4, b));
    return load_u32le(b);
}

ThreadConfig small() {
    ThreadConfig c;
    c.stack_size = 0x1000;
    c.program_capacity = 0x10000;
    return c;
}

struct Fixture {
    MemProvider provider;
    SubopRegistry registry;
    Vm vm{provider, registry};
    Fixture() { register_core_subops(registry); }
};

} // namespace

TEST_CASE("vm arithmetic and store") {
    Fixture f;
    f.provider.put("main", Asm().i32(0x100).i8(6).i8(7).raw({0x22}).store32().ret());
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    auto st = f.vm.run_frame(0);
    REQUIRE(st.has_value());
    CHECK(*st == FrameStatus::AllThreadsDead);
    CHECK(g32(f.vm, 0x100) == 42);
}

TEST_CASE("vm multi and fixed immediate pushes") {
    Fixture f;
    Thread t;
    t.expr.resize(16);
    t.program = {0x03, 0x02, 0x00, 0x7e, 0x80, 0x01, // three SLEB values: 0, -2, 128
                 0x03, 0x80, 0x80,                   // signed 8-bit -128
                 0x03, 0x81, 0x00, 0x80,             // signed 16-bit -32768
                 0x03, 0x82, 0x78, 0x56, 0x34, 0x12,
                 0x03, 0x83, 0xef, 0xcd, 0xab, 0x89, 0x11, 0x22, 0x33, 0x44,
                 0x03, 0x84}; // reserved mode consumes only the mode byte
    for (int i = 0; i < 6; ++i) REQUIRE(f.vm.step(t).has_value());
    CHECK(t.ip == t.program.size());
    REQUIRE(t.depth == 7);
    CHECK(t.expr[0] == 0);
    CHECK(t.expr[1] == 0xfffffffeu);
    CHECK(t.expr[2] == 128);
    CHECK(t.expr[3] == 0xffffff80u);
    CHECK(t.expr[4] == 0xffff8000u);
    CHECK(t.expr[5] == 0x12345678u);
    CHECK(t.expr[6] == 0x89abcdefu);
}

TEST_CASE("vm packed immediate store") {
    Fixture f;
    Asm a;
    a.i32(0x100).raw({0x0d, 0x06}); // 32-bit 1
    a.i32(0x104).raw({0x0d, 0x79}); // 16-bit -2
    a.i32(0x106).raw({0x0d, 0x80, 0x04}); // 8-bit 128, multi-byte encoding
    a.i32(0x107).raw({0x0d, 0x07}); // width code 3 falls back to 8-bit
    a.ret();
    f.provider.put("main", a);
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(g32(f.vm, 0x100) == 1);
    CHECK(g32(f.vm, 0x104) == 0x0180fffeu);
}

TEST_CASE("vm stack-relative packed immediate store") {
    Fixture f;
    Thread t;
    t.stack.resize(0x6000, 0xcc);
    t.expr.resize(8);
    t.expr[0] = 0x12345678;
    t.depth = 1;
    t.sp = 0x5000;
    t.program = {0x0e, 0x10, 0x40, 0x79,       // [sp-0x4010] = -2, 16-bit
                 0x0e, 0x08, 0x00, 0x80, 0x04, // [sp-8] = 128, 8-bit
                 0x0e, 0x04, 0x00, 0x06,       // [sp-4] = 1, 32-bit
                 0x0e, 0x01, 0x00, 0x07};      // [sp-1] = 1, width code 3
    for (int i = 0; i < 4; ++i) REQUIRE(f.vm.step(t).has_value());
    CHECK(t.stack[0x0ff0] == 0xfe);
    CHECK(t.stack[0x0ff1] == 0xff);
    CHECK(t.stack[0x4ff0] == 0xcc); // displacement uses all 16 bits
    CHECK(t.stack[0x4ff8] == 0x80);
    CHECK(load_u32le(t.stack.data() + 0x4ffc) == 0x01000001u);
    CHECK(t.ip == t.program.size());
    CHECK(t.depth == 1);
    CHECK(t.expr[0] == 0x12345678);
}

TEST_CASE("vm relative jump uses the opcode address") {
    Fixture f;
    Thread t;
    t.expr.resize(4);
    t.program = {0x13, 0x05, 0x00, 0, 0, 0x13, 0xfb, 0xff};
    REQUIRE(f.vm.step(t).has_value());
    CHECK(t.ip == 5);
    REQUIRE(f.vm.step(t).has_value());
    CHECK(t.ip == 0);
    CHECK(t.depth == 0);
}

TEST_CASE("vm push several stack-relative values") {
    Fixture f;
    Thread t;
    t.stack.resize(0x100);
    t.expr.resize(8);
    t.sp = 0x100;
    t.stack[0xfc] = 0x80;
    t.stack[0xfa] = 0xfe; t.stack[0xfb] = 0xff;
    t.stack[0xf8] = 0x34; t.stack[0xf9] = 0x12;
    t.program = {0xe3, 0x02, 0x04, 0x00, 0x06, 0x40, 0x08, 0x80};
    REQUIRE(f.vm.step(t).has_value());
    REQUIRE(t.depth == 3);
    CHECK(t.expr[0] == 0xffffff80u);
    CHECK(t.expr[1] == 0xfffffffeu);
    CHECK(t.expr[2] == 0xfffe1234u);
    CHECK(t.ip == t.program.size());
}

TEST_CASE("vm load through local address plus packed displacement") {
    Fixture f;
    Thread t;
    t.stack.resize(0x100);
    t.expr.resize(8);
    t.sp = 0x100;
    t.stack[0xfc] = 0x00; t.stack[0xfd] = 0x01; // 32-bit base = 0x100
    const std::uint8_t values[] = {0x80, 0xfe, 0xff};
    REQUIRE(f.vm.global.write(0xfe, sizeof values, values));
    t.program = {0xe4, 0x04, 0x80, 0x78, // signed 8-bit [0x100 - 2]
                 0xe4, 0x04, 0x80, 0x7d}; // signed 16-bit [0x100 - 1]
    REQUIRE(f.vm.step(t).has_value());
    REQUIRE(f.vm.step(t).has_value());
    REQUIRE(t.depth == 2);
    CHECK(t.expr[0] == 0xffffff80u);
    CHECK(t.expr[1] == 0xfffffffeu);
    CHECK(t.ip == t.program.size());
}

TEST_CASE("vm load through base plus scaled local") {
    Fixture f;
    Thread t;
    t.stack.resize(0x100);
    t.expr.resize(8);
    t.sp = 0x100;
    t.stack[0xfc] = 0xfe; // signed index -2
    const std::uint8_t value[] = {0x34, 0x80};
    REQUIRE(f.vm.global.write(0xf8, sizeof value, value));
    t.expr[0] = 0x100; t.depth = 1;
    t.program = {0xe5, 0x04, 0x00, 0x11}; // [base + (-2)*4], signed 16-bit
    REQUIRE(f.vm.step(t).has_value());
    CHECK(t.depth == 1);
    CHECK(t.expr[0] == 0xffff8034u);
    CHECK(t.ip == t.program.size());
}

TEST_CASE("vm store base plus scaled local") {
    Fixture f;
    Thread t;
    t.stack.resize(0x100);
    t.expr.resize(8);
    t.sp = 0x100;
    t.stack[0xfe] = 0xfe; t.stack[0xff] = 0xff; // signed index -2
    t.expr[0] = 0x100; t.expr[1] = 0x1000; t.depth = 2;
    t.program = {0xe7, 0x02, 0x40, 0x12}; // store 0x1000 + (-2)*4 as a dword
    REQUIRE(f.vm.step(t).has_value());
    CHECK(g32(f.vm, 0x100) == 0xff8);
    CHECK(t.depth == 0);
    CHECK(t.ip == t.program.size());
}

TEST_CASE("vm store local plus packed immediate") {
    Fixture f;
    Thread t;
    t.stack.resize(0x100);
    t.expr.resize(8);
    t.sp = 0x100;
    t.stack[0xfc] = 0xfb; // signed 8-bit -5
    t.stack[0xfa] = 0; t.stack[0xfb] = 0x80; // signed 16-bit -32768
    for (unsigned i = 0xf0; i < 0xf4; ++i) t.stack[i] = 0xff;
    t.program = {0xe6, 0x04, 0x00, 0x0e, // 32-bit destination: -5 + 3
                 0xe6, 0x06, 0x40, 0x75, // 16-bit destination: -32768 - 3
                 0xe6, 0x10, 0x80, 0x06}; // 32-bit destination: 0xffffffff + 1
    t.expr[0] = 0x100; t.depth = 1;
    REQUIRE(f.vm.step(t).has_value());
    t.expr[0] = 0x104; t.depth = 1;
    REQUIRE(f.vm.step(t).has_value());
    t.expr[0] = 0x108; t.depth = 1;
    REQUIRE(f.vm.step(t).has_value());
    CHECK(g32(f.vm, 0x100) == 0xfffffffeu);
    CHECK(g32(f.vm, 0x104) == 0x7ffdu);
    CHECK(g32(f.vm, 0x108) == 0);
    CHECK(t.depth == 0);
    CHECK(t.ip == t.program.size());
}

TEST_CASE("vm signed ops, division by zero and shifts") {
    Fixture f;
    Asm a;
    a.set32(0x100, 0); // placeholder so addresses are mapped
    a.i32(0x104).i8(-7).i8(2).raw({0x23}).store32();           // -7 / 2 = -3 (truncating)
    a.i32(0x108).i8(-7).i8(2).raw({0x24}).store32();           // -7 % 2 = -1
    a.i32(0x10c).i8(5).i8(0).raw({0x23}).store32();            // x / 0 = -1
    a.i32(0x110).i8(-16).i8(2).raw({0x2b}).store32();          // arithmetic shift right
    a.i32(0x114).i8(-16).i8(28).raw({0x2a}).store32();         // logical shift right
    a.ret();
    f.provider.put("main", a);
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(static_cast<std::int32_t>(g32(f.vm, 0x104)) == -3);
    CHECK(static_cast<std::int32_t>(g32(f.vm, 0x108)) == -1);
    CHECK(g32(f.vm, 0x10c) == 0xffffffffu);
    CHECK(static_cast<std::int32_t>(g32(f.vm, 0x110)) == -4);
    CHECK(g32(f.vm, 0x114) == 0xf);
}

TEST_CASE("vm loop with compare-branch") {
    Fixture f;
    Asm a;
    a.set32(0x200, 0);
    Word top = a.pos();
    a.i32(0x200).i32(0x200).load32().i8(1).raw({0x20}).raw({0x09, 0x02}); // [0x200] += 1, keep value
    a.i8(5);
    a.branch(0x3b, 0x81, top); // loop while value != 5
    a.ret();
    f.provider.put("main", a);
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(g32(f.vm, 0x200) == 5);
}

TEST_CASE("vm call and return use the data stack") {
    Fixture f;
    Asm a;
    a.i32(0x300);
    Word call_site = a.pos();
    a.raw({0x06, 0, 0}); // push address of the function (patched below)
    a.raw({0x16});       // call
    a.store32().ret();
    Word func = a.pos();
    a.i8(42).ret();
    std::int32_t d = static_cast<std::int32_t>(func) - static_cast<std::int32_t>(call_site);
    a.b[call_site + 1] = static_cast<std::uint8_t>(d);
    a.b[call_site + 2] = static_cast<std::uint8_t>(d >> 8);
    f.provider.put("main", a);
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(g32(f.vm, 0x300) == 42);
}

TEST_CASE("scheduler: yield hands over to the next thread, new frame resumes") {
    Fixture f;
    f.provider.put("a", Asm().set32(0x400, 1).sub(0x80, 0x5f).set32(0x404, 1).ret());
    Asm b;
    b.i32(0x408).i32(0x400).load32().store32();
    b.i32(0x40c).i32(0x404).load32().store32().ret();
    f.provider.put("b", b);
    REQUIRE(f.vm.spawn(small(), "x.arc", "a"));
    REQUIRE(f.vm.spawn(small(), "x.arc", "b"));
    auto st = f.vm.run_frame(0);
    REQUIRE(st.has_value());
    CHECK(*st == FrameStatus::Ok);
    CHECK(g32(f.vm, 0x408) == 1); // b ran after a's first part
    CHECK(g32(f.vm, 0x40c) == 0); // but before a's second part
    st = f.vm.run_frame(16);
    REQUIRE(st.has_value());
    CHECK(*st == FrameStatus::AllThreadsDead);
    CHECK(g32(f.vm, 0x404) == 1);
}

TEST_CASE("thread messages") {
    Fixture f;
    f.provider.put("a", Asm().i8(2).i8(77).sub(0x80, 0x48).ret()); // send 77 to thread 2 (ids start at 1)
    f.provider.put("b", Asm().i32(0x500).sub(0x80, 0x49).ret());   // receive into [0x500]
    REQUIRE(f.vm.spawn(small(), "x.arc", "a"));
    REQUIRE(f.vm.spawn(small(), "x.arc", "b"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(g32(f.vm, 0x500) == 77);
    CHECK(f.vm.find(1) == nullptr); // thread 1 finished
}

TEST_CASE("message batches take the buffer address from the top of the stack") {
    Fixture f;
    // thread 1 sends two words to thread 2 (80:4A pops array, count, thread); thread 2 receives them (80:4B pops buffer, then limit)
    Asm a;
    a.set32(0x700, 5).set32(0x704, 6);
    a.i8(2).i8(2).i32(0x700).sub(0x80, 0x4a).ret();
    f.provider.put("a", a);
    f.provider.put("b", Asm().i8(8).i32(0x800).sub(0x80, 0x4b).i32(0x810).i32(0).store32().ret()); // [0x810] stays as a marker
    REQUIRE(f.vm.spawn(small(), "x.arc", "a"));
    REQUIRE(f.vm.spawn(small(), "x.arc", "b"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(g32(f.vm, 0x800) == 5);
    CHECK(g32(f.vm, 0x804) == 6);
}

TEST_CASE("unloading a program block counts the blocks left, not the first one") {
    Fixture f;
    f.provider.put("lib", Asm().ret());
    f.provider.put("main", Asm().i32(0x300).i32(0x310).sub(0x80, 0x40).i8(0).sub(0x80, 0x41).sub(0x80, 0x5f).ret());
    f.provider.put("lib", Asm().ret());
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    Thread* t = f.vm.find(1);
    REQUIRE(t != nullptr);
    f.vm.global.write(0x300, 4, reinterpret_cast<const std::uint8_t*>("x.arc"));
    f.vm.global.write(0x310, 4, reinterpret_cast<const std::uint8_t*>("lib"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(t->modules.size() == 1); // loaded and unloaded again; only the first block is left
}

TEST_CASE("script global memory does not exist before it is allocated when the host asks for that") {
    Fixture f;
    f.vm.global_ready = false;
    f.provider.put("main", Asm().set32(0x500, 9).i32(0x500).load32().i32(0x504).i32(0).store32().ret());
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    REQUIRE(f.vm.run_frame(0).has_value());
    f.vm.global_ready = true;
    CHECK(g32(f.vm, 0x500) == 0); // the write was lost
}

TEST_CASE("deadline wait resumes on the virtual clock") {
    Fixture f;
    Asm a;
    a.i8(50).sub(0x80, 0x58);
    Word wait = a.pos();
    a.sub(0x80, 0x5a);
    a.branch(0x15, 0x08, wait); // while remaining != 0
    a.set32(0x600, 1).ret();
    f.provider.put("main", a);
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(g32(f.vm, 0x600) == 0);
    REQUIRE(f.vm.run_frame(20).has_value());
    CHECK(g32(f.vm, 0x600) == 0);
    auto st = f.vm.run_frame(60);
    REQUIRE(st.has_value());
    CHECK(*st == FrameStatus::AllThreadsDead);
    CHECK(g32(f.vm, 0x600) == 1);
}

TEST_CASE("unimplemented sub-opcodes are hard errors") {
    Fixture f;
    f.provider.put("main", Asm().sub(0x80, 0xfd).ret());
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    auto st = f.vm.run_frame(0);
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error().message.find("80:FD") != std::string::npos);
}

TEST_CASE("registry checks the declared stack effect") {
    Fixture f;
    f.registry.add({0x9099, "liar", 0, 1, [](Ctx&) { return 0; }});
    f.provider.put("main", Asm().sub(0x90, 0x99).ret());
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    auto st = f.vm.run_frame(0);
    REQUIRE_FALSE(st.has_value());
    CHECK(st.error().message.find("registry says") != std::string::npos);
}

TEST_CASE("the expression stack is a ring; memory faults are reported") {
    Fixture f;
    f.provider.put("a", Asm().raw({0x20}).ret()); // add with an empty stack: pops wrap round, as in the original
    REQUIRE(f.vm.spawn(small(), "x.arc", "a"));
    CHECK(f.vm.run_frame(0).has_value());

    // pushing more than the stack holds wraps instead of failing
    Fixture w;
    Asm many;
    for (int i = 0; i < 0x1000 + 10; ++i) many.i8(1);
    w.provider.put("a", many.ret());
    ThreadConfig cfg = small();
    cfg.expr_slots = 16;
    REQUIRE(w.vm.spawn(cfg, "x.arc", "a"));
    CHECK(w.vm.run_frame(0).has_value());

    Fixture g;
    g.provider.put("a", Asm().i32(make_addr(Tag::Stack, 0x2000)).raw({0x08, 0x02}).ret());
    REQUIRE(g.vm.spawn(small(), "x.arc", "a")); // stack is only 0x1000 bytes
    CHECK_FALSE(g.vm.run_frame(0).has_value());
}

TEST_CASE("boot: autoload ipl loads system ipl, then stops at the first unimplemented sub-opcode" * doctest::test_suite("game")) {
    REQUIRE(std::filesystem::exists(obgi_test::game_dir()));
    ArcProgramProvider provider(obgi_test::game_dir());
    SubopRegistry registry;
    register_core_subops(registry);
    Vm vm(provider, registry);
    auto t = vm.spawn(ThreadConfig{}, "autoload.arc", "ipl._bp");
    REQUIRE_MESSAGE(t.has_value(), std::string(t ? "" : t.error().message));
    auto st = vm.run_frame(0);
    REQUIRE_FALSE(st.has_value());
    // The research trace shows 80:40 loading system.arc/ipl._bp, then the first unimplemented one is 80:FD.
    CHECK(st.error().message.find("80:FD") != std::string::npos);
    CHECK((*t)->modules.size() == 2);
    CHECK((*t)->modules[1].arc == "system.arc");
}

TEST_CASE("vm store 0A pops the address first, 09 the value first") {
    Fixture f;
    // 0A: [value][address] store ; 09: [address][value] store, keeping the value
    f.provider.put("main", Asm().i32(0x1234).i32(0x100).raw({0x0a, 0x02}).i32(0x104).i32(0x5678).raw({0x09, 0x02}).i32(0x108).raw({0x0a, 0x02}).ret());
    REQUIRE(f.vm.spawn(small(), "x.arc", "main"));
    REQUIRE(f.vm.run_frame(0).has_value());
    CHECK(g32(f.vm, 0x100) == 0x1234);
    CHECK(g32(f.vm, 0x104) == 0x5678);
    CHECK(g32(f.vm, 0x108) == 0x5678); // the value 09 left on the stack went to 0x108 through 0A
}
