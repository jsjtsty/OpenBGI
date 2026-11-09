#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "vm/vm.hpp"

namespace obgi::vm {

namespace {
bool trace_panel_enabled() {
    static const bool enabled = std::getenv("OBGI_TRACEPANEL") != nullptr;
    return enabled;
}
} // namespace

void Ctx::suspend(WaitObject wait, std::uint8_t results) {
    if (!ok()) return;
    vm.cancel_wait(t);
    wait.result_count = results;
    t.wait = std::move(wait);
    if (t.wait.kind == WaitObject::Kind::InputTime && t.wait.interrupt_input && vm.wait_input)
        vm.wait_input->begin(t.wait.input_priority);
}

Result<Word> HeapAllocator::allocate(PagedSegment&, Word size) {
    const std::uint64_t need = (std::uint64_t(size ? size : 1) + 15) & ~std::uint64_t(15);
    std::uint64_t at = 0x10; // keep offset 0 unused so no block looks like a null address
    for (const auto& [start, len] : blocks_) {
        if (start >= at + need) break;
        at = std::max<std::uint64_t>(at, std::uint64_t(start) + ((std::uint64_t(len ? len : 1) + 15) & ~std::uint64_t(15)));
    }
    if (at + need > kOffsetMask) return fail(Errc::Range, "script heap exhausted");
    blocks_[static_cast<Word>(at)] = size;
    return static_cast<Word>(at);
}

bool HeapAllocator::release(PagedSegment& heap, Word offset) {
    auto it = blocks_.find(offset);
    if (it == blocks_.end()) return false;
    heap.zero(offset, it->second);
    blocks_.erase(it);
    return true;
}

namespace {
std::string hex(Word v) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08X", v);
    return buf;
}
} // namespace

Word Ctx::pop() {
    if (!ok()) return 0;
    // The expression stack is a ring (FUN_0044d2a0 / FUN_0044d2c0): scripts leave values behind and pop below
    // their own base without harm, so neither end is an error.
    if (t.depth == 0) t.depth = static_cast<Word>(t.expr.size());
    return t.expr[--t.depth];
}

void Ctx::push(Word v) {
    if (!ok()) return;
    t.expr[t.depth] = v;
    t.depth = t.depth + 1 < t.expr.size() ? t.depth + 1 : 0;
}

std::int64_t Ctx::read(Word addr, unsigned width, bool sign_extend) {
    std::uint8_t buf[4] = {};
    if (!ok()) return 0;
    Word off = addr & kOffsetMask;
    bool good = false;
    switch (static_cast<Tag>(addr >> kTagShift)) {
    case Tag::Global: good = !vm.global_ready || vm.global.read(off, width, buf); break;
    case Tag::Heap: good = vm.heap.read(off, width, buf); break;
    case Tag::Program:
        good = static_cast<std::uint64_t>(off) + width <= t.program.size();
        if (good) std::copy_n(t.program.data() + off, width, buf);
        break;
    case Tag::Stack:
        good = static_cast<std::uint64_t>(off) + width <= t.stack.size();
        if (good) std::copy_n(t.stack.data() + off, width, buf);
        break;
    }
    if (!good) {
        fail(Errc::Range, "read out of bounds: " + hex(addr) + "+" + std::to_string(width));
        return 0;
    }
    std::uint32_t raw = buf[0] | (buf[1] << 8) | (buf[2] << 16) | (static_cast<std::uint32_t>(buf[3]) << 24);
    if (!sign_extend || width == 4) return raw;
    unsigned bits = width * 8;
    return static_cast<std::int64_t>(static_cast<std::int32_t>(raw << (32 - bits)) >> (32 - bits));
}

void Ctx::write(Word addr, Word value, unsigned width) {
    const Word off = addr & kOffsetMask;
    if (trace_panel_enabled() && (addr >> kTagShift) == static_cast<Word>(Tag::Global) &&
        (off == 0x65C || off == 0xD30 || off == 0xD34 || off == 0xD94)) {
        const Word before = static_cast<Word>(read(addr, width, false));
        std::fprintf(stderr, "PANEL-GLOBAL-WRITE t%u ip=%X", t.id, t.ip);
        for (const auto& module : t.modules) if (t.ip >= module.start && t.ip < module.start + module.size) {
            std::fprintf(stderr, " module=%s/%s+%X", module.arc.c_str(), module.name.c_str(), t.ip - module.start);
            break;
        }
        std::fprintf(stderr, " addr=%08X width=%u before=%08X value=%08X\n", addr, width, before, value);
    }
    std::uint8_t buf[4] = {std::uint8_t(value), std::uint8_t(value >> 8), std::uint8_t(value >> 16),
                           std::uint8_t(value >> 24)};
    write_bytes(addr, buf, width);
}

void Ctx::read_bytes(Word addr, std::size_t n, std::vector<std::uint8_t>& out) {
    out.assign(n, 0);
    if (!ok() || !n) return;
    Word off = addr & kOffsetMask;
    bool good = false;
    switch (static_cast<Tag>(addr >> kTagShift)) {
    case Tag::Global: good = !vm.global_ready || vm.global.read(off, n, out.data()); break;
    case Tag::Heap: good = vm.heap.read(off, n, out.data()); break;
    case Tag::Program:
        good = static_cast<std::uint64_t>(off) + n <= t.program.size();
        if (good) std::copy_n(t.program.data() + off, n, out.data());
        break;
    case Tag::Stack:
        good = static_cast<std::uint64_t>(off) + n <= t.stack.size();
        if (good) std::copy_n(t.stack.data() + off, n, out.data());
        break;
    }
    if (!good) fail(Errc::Range, "read out of bounds: " + hex(addr) + "+" + std::to_string(n));
}

void Ctx::write_bytes(Word addr, const std::uint8_t* data, std::size_t n) {
    static const char* watch_env = std::getenv("OBGI_WATCH"); // hex address: print every write that touches it
    if (watch_env) {
        const auto watch = static_cast<Word>(std::strtoul(watch_env, nullptr, 16));
        if (addr <= watch && watch < addr + n) {
            std::fprintf(stderr, "write [%X..+%zu] thread %u ip=%X:", addr, n, t.id, t.ip);
            for (std::size_t i = 0; i < n && i < 8; ++i) std::fprintf(stderr, " %02X", data[i]);
            std::fprintf(stderr, "\n");
        }
    }
    if (!ok() || !n) return;
    Word off = addr & kOffsetMask;
    bool good = false;
    switch (static_cast<Tag>(addr >> kTagShift)) {
    case Tag::Global: good = !vm.global_ready || vm.global.write(off, n, data); break;
    case Tag::Heap: good = vm.heap.write(off, n, data); break;
    case Tag::Program:
        good = static_cast<std::uint64_t>(off) + n <= t.program.size();
        if (good) std::copy_n(data, n, t.program.data() + off);
        break;
    case Tag::Stack:
        good = static_cast<std::uint64_t>(off) + n <= t.stack.size();
        if (good) std::copy_n(data, n, t.stack.data() + off);
        break;
    }
    if (!good) fail(Errc::Range, "write out of bounds: " + hex(addr) + "+" + std::to_string(n));
}

std::string Ctx::cstring(Word addr, std::size_t max) {
    std::string s;
    for (std::size_t i = 0; i < max && ok(); ++i) {
        auto c = static_cast<std::uint8_t>(read(addr + static_cast<Word>(i), 1, false));
        if (!c) return s;
        s.push_back(static_cast<char>(c));
    }
    if (ok()) fail(Errc::Format, "unterminated string at " + hex(addr));
    return s;
}

void Ctx::write_cstring(Word addr, std::string_view s) {
    std::vector<std::uint8_t> bytes(s.begin(), s.end());
    bytes.push_back(0);
    write_bytes(addr, bytes.data(), bytes.size());
}

} // namespace obgi::vm
