#include <cmath>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "vm/vm.hpp"

namespace obgi::vm {

namespace {

bool trace_panel_enabled() {
    static const bool enabled = std::getenv("OBGI_TRACEPANEL") != nullptr;
    return enabled;
}

// Operand reader over the thread's program area; failures are sticky like in Ctx.
struct Reader {
    Ctx& c;
    const std::vector<std::uint8_t>& prog;
    Word p;

    std::uint8_t u8() {
        if (p >= prog.size()) {
            c.fail(Errc::Range, "instruction operand beyond program end");
            return 0;
        }
        return prog[p++];
    }
    std::uint16_t u16() {
        unsigned lo = u8();
        return static_cast<std::uint16_t>(lo | (u8() << 8));
    }
    Word u32() {
        Word lo = u16();
        return lo | (static_cast<Word>(u16()) << 16);
    }
    std::int64_t sleb() { // 7-bit groups, little-endian, sign from bit 6 of the last byte
        std::int64_t v = 0;
        unsigned shift = 0;
        for (int i = 0; i < 5; ++i) {
            std::uint8_t b = u8();
            v |= static_cast<std::int64_t>(b & 127) << shift;
            shift += 7;
            if (b < 128) return (b & 64) ? v - (std::int64_t{1} << shift) : v;
        }
        c.fail(Errc::Format, "variable-length operand longer than five bytes");
        return 0;
    }
};

std::int32_t s32(Word v) { return static_cast<std::int32_t>(v); }
std::int16_t s16(unsigned v) { return static_cast<std::int16_t>(v); }
unsigned width_of(unsigned idx) {
    static constexpr unsigned w[4] = {1, 2, 4, 1};
    return w[idx & 3];
}

Word shift_value(std::int32_t val, std::int32_t shift) {
    if (shift >= 0) return shift >= 32 ? 0 : static_cast<Word>(static_cast<std::uint32_t>(val) << shift);
    return static_cast<Word>(val >> std::min(-shift, 31));
}

bool is_prefix(std::uint8_t op) {
    switch (op) {
    case 0x7f: case 0x80: case 0x81: case 0x90: case 0x91: case 0x92:
    case 0xa0: case 0xb0: case 0xc0: case 0xd0: case 0xe0: return true;
    default: return false;
    }
}

// Jump targets may arrive as plain offsets or as tagged program addresses.
Word code_target(Ctx& c, Word v) {
    Word tag = v >> kTagShift;
    if (tag == 0 || tag == static_cast<Word>(Tag::Program)) return v & kOffsetMask;
    c.fail(Errc::Range, "jump target is not a program address");
    return 0;
}

std::string format_script(Ctx& c, const std::string& fmt) {
    std::string out;
    for (std::size_t i = 0; i < fmt.size() && c.ok(); ++i) {
        if (fmt[i] != '%') {
            out.push_back(fmt[i]);
            continue;
        }
        std::size_t j = i + 1;
        while (j < fmt.size() && std::strchr("0123456789 .-", fmt[j])) ++j;
        char conv = j < fmt.size() ? fmt[j] : 0;
        if (conv == '%' && j == i + 1) {
            out.push_back('%');
            i = j;
            continue;
        }
        if (!conv || !std::strchr("sdxX", conv)) {
            c.fail(Errc::Unsupported, "unsupported format specifier in script string");
            break;
        }
        std::string spec = fmt.substr(i, j - i + 1);
        Word arg = c.pop();
        std::string text;
        if (conv == 's') {
            text = c.cstring(arg);
            if (!c.ok()) break;
        }
        int n = conv == 's' ? std::snprintf(nullptr, 0, spec.c_str(), text.c_str())
                : conv == 'd' ? std::snprintf(nullptr, 0, spec.c_str(), s32(arg))
                              : std::snprintf(nullptr, 0, spec.c_str(), arg);
        std::string buf(static_cast<std::size_t>(n) + 1, '\0');
        if (conv == 's') std::snprintf(buf.data(), buf.size(), spec.c_str(), text.c_str());
        else if (conv == 'd') std::snprintf(buf.data(), buf.size(), spec.c_str(), s32(arg));
        else std::snprintf(buf.data(), buf.size(), spec.c_str(), arg);
        out.append(buf.data(), static_cast<std::size_t>(n));
        i = j;
    }
    return out;
}

constexpr Word kMaxBlock = 0x1000000;

} // namespace

Result<int> Vm::step(Thread& t) {
    Ctx c(*this, t);
    const Word pc = t.ip;
    Reader r{c, t.program, pc};
    const std::uint8_t op = r.u8();
    int rc = Continue;
    if (trace_op) trace_op(t, op);
    auto stack_addr = [&](unsigned operand) { return make_addr(Tag::Stack, t.sp - (operand & 0x3fff)); };
    const bool trace_panel_thread = t.id == 20 &&
        ((pc >= 0xC2C0 && pc < 0xC340) || (pc >= 0xC500 && pc < 0xC900) || (pc >= 0xEA7E && pc <= 0xEE8D) || (pc >= 0xF00 && pc < 0xF80));
    const bool trace_message_writer = t.id == 1 &&
        ((pc >= 0x14400 && pc < 0x14800) || (pc >= 0x16200 && pc < 0x16300));
    if (trace_panel_enabled() && (trace_panel_thread || trace_message_writer)) {
        std::fprintf(stderr, "VM-STEP t%u pc=%X op=%02X sp=%X", t.id, pc, op, t.sp);
        for (const auto& module : t.modules) if (pc >= module.start && pc < module.start + module.size) {
            std::fprintf(stderr, " module=%s/%s+%X", module.arc.c_str(), module.name.c_str(), pc - module.start);
            break;
        }
        for (Word i = 1; i <= 4 && i <= t.depth; ++i)
            std::fprintf(stderr, " e%u=%X", i, t.expr[(t.depth + t.expr.size() - i) % t.expr.size()]);
        for (Word off = 4; off <= 16 && off <= t.sp; off += 4)
            std::fprintf(stderr, " s%u=%X", off, static_cast<Word>(c.read(make_addr(Tag::Stack, t.sp - off), 4, false)));
        if (pc == 0xC58E) {
            for (unsigned off : {0xA368, 0xA37C, 0xA378, 0xA370, 0xA36C, 0xA6B0})
                std::fprintf(stderr, " local[%X]=%X", off,
                             static_cast<Word>(c.read(stack_addr(off), 4, false)));
        }
        std::fputc('\n', stderr);
    }

    // Decode operands first (they all precede execution), then run. `end` is the next instruction.
    if (is_prefix(op)) {
        std::uint8_t sub = r.u8();
        if (!c.ok()) return std::unexpected(*c.error);
        t.ip = r.p;
        const std::uint16_t id = static_cast<std::uint16_t>(op << 8 | sub);
        const SubopDef* def = registry.find(id);
        if (!def) return fail(Errc::Unsupported, "sub-opcode " + subop_name(id) + " is not implemented");
        const Word depth_before = t.depth;
        rc = def->fn(c);
        if (!c.ok()) return fail(c.error->code, def->name + " (" + subop_name(id) + "): " + c.error->message);
        const std::int64_t ring = static_cast<std::int64_t>(t.expr.size());
        const std::int64_t moved = ((static_cast<std::int64_t>(t.depth) - static_cast<std::int64_t>(depth_before)) % ring + ring) % ring;
        const auto deferred = t.wait.result_count;
        if (deferred > def->completion_pushes || deferred > def->pushes)
            return fail(Errc::Format, "invalid completion signature for " + subop_name(id));
        const std::int64_t expected = ((std::int64_t(def->pushes) - deferred - std::int64_t(def->pops)) % ring + ring) % ring;
        if (moved != expected)
            return fail(Errc::Format, "sub-opcode " + subop_name(id) + " moved the expression stack by " +
                                          std::to_string(static_cast<int>(moved)) +
                                          ", registry says " + std::to_string(def->pushes - def->pops));
        if (t.wait.kind != WaitObject::Kind::None) {
            t.wait.opcode = id;
            t.wait.resume_depth = t.depth;
        }
        if (trace) {
            std::vector<Word> out;
            for (unsigned i = 0; i < unsigned(def->pushes - deferred); ++i) out.insert(out.begin(), t.expr[(t.depth + t.expr.size() - 1 - i) % t.expr.size()]);
            trace(t, id, out);
        }
        return rc;
    }

    switch (op) {
    case 0x00: { auto v = static_cast<std::int8_t>(r.u8()); t.ip = r.p; c.push(static_cast<Word>(v)); break; }
    case 0x01: { auto v = s16(r.u16()); t.ip = r.p; c.push(static_cast<Word>(static_cast<std::int32_t>(v))); break; }
    case 0x02: { Word v = r.u32(); t.ip = r.p; c.push(v); break; }
    case 0x04: { unsigned v = r.u16(); t.ip = r.p; c.push(make_addr(Tag::Stack, t.sp - v)); break; }
    case 0x05: { unsigned v = r.u16(); t.ip = r.p; c.push((pc + static_cast<Word>(s16(v))) | make_addr(Tag::Program, 0)); break; }
    case 0x06: { unsigned v = r.u16(); t.ip = r.p; c.push(pc + static_cast<Word>(static_cast<std::int32_t>(s16(v)))); break; }
    case 0x08: {
        unsigned w = r.u8(); t.ip = r.p;
        w = w <= 2 ? (1u << w) : 1;
        Word addr = c.pop();
        c.push(static_cast<Word>(c.read(addr, w, w < 4)));
        break;
    }
    case 0x09: case 0x0a: {
        unsigned w = r.u8(); t.ip = r.p;
        w = w <= 2 ? (1u << w) : 1;
        // 09 (store, keeps the value) takes the value from the top; 0A takes the address from the top (FUN_00482330 vs
        // FUN_00482390 pop in opposite orders), which is how the scripts' `*p = expr` statements are compiled
        Word val, addr;
        if (op == 0x09) val = c.pop(), addr = c.pop();
        else addr = c.pop(), val = c.pop();
        c.write(addr, val, w);
        if (op == 0x09) c.push(val);
        break;
    }
    case 0x0c: {
        unsigned w = r.u8(), n = r.u8(); t.ip = r.p;
        if (w > 2) { c.fail(Errc::Format, "bad store width"); break; }
        w = 1u << w;
        std::vector<Word> values(n);
        for (auto& v : values) v = c.pop();
        Word addr = c.pop();
        for (unsigned i = 0; i < n; ++i) c.write(addr + i * w, values[n - 1 - i], w);
        break;
    }
    case 0x0f: {
        unsigned v = r.u16(); t.ip = r.p;
        c.write(stack_addr(v), c.pop(), width_of(v >> 14));
        break;
    }
    case 0x10: // push the current data-frame cursor as an untagged offset
        t.ip = r.p;
        c.push(t.sp);
        break;
    case 0x11: { // restore/set the data-frame cursor from an untagged offset
        t.ip = r.p;
        Word sp = c.pop();
        if (c.ok()) {
            if (sp >= t.stack.size()) c.fail(Errc::Range, "frame pointer is outside the thread data area");
            else t.sp = sp;
        }
        break;
    }
    case 0x12: { auto v = r.sleb(); t.ip = r.p; t.sp = static_cast<Word>(t.sp + v); break; }
    case 0x14: { t.ip = r.p; t.ip = code_target(c, c.pop()); break; }
    case 0x15: {
        unsigned mode = r.u8();
        Word dest = 0;
        if (mode & 8) dest = pc + static_cast<Word>(static_cast<std::int32_t>(s16(r.u16())));
        t.ip = r.p;
        if (!(mode & 8)) dest = code_target(c, c.pop());
        std::int32_t a = s32(c.pop());
        bool conds[6] = {a != 0, a == 0, a > 0, a >= 0, a <= 0, a < 0};
        if ((mode & 7) >= 6) { c.fail(Errc::Unsupported, "unsupported branch mode"); break; }
        if (trace_panel_enabled() && (trace_panel_thread || trace_message_writer) &&
            ((pc >= 0xC500 && pc < 0xC900) || (pc >= 0xEA7E && pc <= 0xEE8D) || (pc >= 0xF00 && pc < 0xF80) || trace_message_writer))
            std::fprintf(stderr, "VM-BRANCH1 t%u pc=%X mode=%02X value=%X cond=%d target=%X\n", t.id, pc, mode, static_cast<Word>(a), conds[mode & 7], conds[mode & 7] ? dest : t.ip);
        if (conds[mode & 7]) t.ip = dest;
        break;
    }
    case 0x16: {
        t.ip = r.p;
        Word dest = code_target(c, c.pop());
        if (trace_panel_enabled() && ((t.id == 20 && ((pc >= 0xB700 && pc < 0xB900) || (pc >= 0xC2C0 && pc < 0xC340) || (pc >= 0xC500 && pc < 0xC900) || (pc >= 0xEA7E && pc <= 0xEE8D) || (pc >= 0xF00 && pc < 0xF80) || (pc >= 0x19000 && pc < 0x1A100))) || trace_message_writer))
            std::fprintf(stderr, "VM-CALL t%u pc=%X dest=%X depth=%zu ret=%X\n", t.id, pc, dest, t.calls.size(), t.ip);
        c.write(make_addr(Tag::Stack, t.sp), t.ip, 4);
        t.sp += 4;
        t.calls.push_back({pc, t.ip});
        t.ip = dest;
        break;
    }
    case 0x17: {
        t.ip = r.p;
        if (t.calls.empty()) { rc = ExitThread; break; }
        t.sp -= 4;
        Word dest = static_cast<Word>(c.read(make_addr(Tag::Stack, t.sp), 4, false));
        Thread::Call call = t.calls.back();
        t.calls.pop_back();
        if (trace_panel_enabled() && ((t.id == 20 && ((pc >= 0xB700 && pc < 0xB900) || (pc >= 0xC2C0 && pc < 0xC340) || (pc >= 0xC500 && pc < 0xC900) || (pc >= 0xEA7E && pc <= 0xEE8D) || (pc >= 0xF00 && pc < 0xF80) || (pc >= 0x19000 && pc < 0x1A100) || (dest >= 0xC600 && dest < 0xC900))) || trace_message_writer))
            std::fprintf(stderr, "VM-RET t%u pc=%X origin=%X ret=%X depth=%zu\n", t.id, pc, call.origin, dest, t.calls.size());
        if (c.ok() && dest != call.ret) c.fail(Errc::Format, "return address does not match the call record");
        t.ip = dest;
        break;
    }
    case 0x18: {
        Word enc = r.u32(); t.ip = r.p;
        unsigned w = width_of(enc >> 30);
        c.push(static_cast<Word>(c.read(enc & 0x3fffffff, w, w < 4)));
        break;
    }
    case 0x19: {
        unsigned v = r.u16(); t.ip = r.p;
        unsigned w = width_of(v >> 14);
        c.push(static_cast<Word>(c.read(stack_addr(v), w, w < 4)));
        break;
    }
    case 0x1a: case 0x1b: case 0x1c: case 0x1d: case 0x1e: {
        unsigned v = r.u16();
        std::int64_t imm = 0;
        std::int32_t shift = 0;
        if (op == 0x1a || op == 0x1b) imm = r.sleb();
        if (op == 0x1c) shift = static_cast<std::int8_t>(r.u8());
        t.ip = r.p;
        unsigned w = width_of(v >> 14);
        std::int64_t val = c.read(stack_addr(v), w, w < 4);
        if (op == 0x1a) c.push(static_cast<Word>(val + imm));
        else if (op == 0x1b) { Word top = c.pop(); c.push(static_cast<Word>(val * imm + top)); }
        else if (op == 0x1c) c.push(shift_value(s32(static_cast<Word>(val)), shift));
        else if (op == 0x1d) c.push(static_cast<Word>(val * static_cast<std::int64_t>(c.pop())));
        else {
            std::int64_t num = s32(c.pop());
            c.push(val == 0 ? Word(-1) : static_cast<Word>(num / val));
        }
        break;
    }
    case 0x1f: {
        std::int64_t imm = r.sleb(); t.ip = r.p;
        Word addr = static_cast<Word>((imm >> 2) + c.pop());
        unsigned w = width_of(static_cast<unsigned>(imm & 3));
        c.push(static_cast<Word>(c.read(addr, w, w < 4)));
        break;
    }
    case 0x20: case 0x21: case 0x22: case 0x25: case 0x26: case 0x27:
    case 0x30: case 0x31: case 0x32: case 0x33: case 0x34: case 0x35: {
        t.ip = r.p;
        Word a = c.pop(), b = c.pop(), res = 0;
        switch (op) {
        case 0x20: res = b + a; break;
        case 0x21: res = b - a; break;
        case 0x22: res = b * a; break;
        case 0x25: res = b & a; break;
        case 0x26: res = b | a; break;
        case 0x27: res = b ^ a; break;
        case 0x30: res = b == a; break;
        case 0x31: res = b != a; break;
        case 0x32: res = s32(b) <= s32(a); break;
        case 0x33: res = s32(b) >= s32(a); break;
        case 0x34: res = s32(b) < s32(a); break;
        default: res = s32(b) > s32(a); break;
        }
        c.push(res);
        break;
    }
    case 0x23: case 0x24: {
        t.ip = r.p;
        std::int64_t a = s32(c.pop()), b = s32(c.pop());
        if (!a) c.push(Word(-1));
        else {
            std::int64_t q = b / a; // truncates toward zero, like the original sign handling
            c.push(static_cast<Word>(op == 0x23 ? q : b - q * a));
        }
        break;
    }
    case 0x28: { t.ip = r.p; c.push(~c.pop()); break; }
    case 0x29: case 0x2a: case 0x2b: {
        t.ip = r.p;
        Word a = c.pop() & 31, b = c.pop();
        c.push(op == 0x29 ? b << a : op == 0x2a ? b >> a : static_cast<Word>(s32(b) >> a));
        break;
    }
    case 0x2c: { auto imm = r.sleb(); t.ip = r.p; c.push(static_cast<Word>(c.pop() + imm)); break; }
    case 0x2d: { auto imm = r.sleb(); t.ip = r.p; c.push(static_cast<Word>(static_cast<std::int64_t>(c.pop()) * imm)); break; }
    case 0x2e: {
        auto imm = r.sleb(); t.ip = r.p;
        Word a = c.pop(), b = c.pop();
        c.push(static_cast<Word>(static_cast<std::int64_t>(a) * imm + b));
        break;
    }
    case 0x2f: {
        auto div = r.sleb(); t.ip = r.p;
        std::int64_t n = s32(c.pop());
        c.push(div == 0 ? Word(-1) : static_cast<Word>(n / div));
        break;
    }
    case 0x38: case 0x39: {
        t.ip = r.p;
        Word a = c.pop(), b = c.pop();
        c.push(op == 0x38 ? (a && b) : (a || b));
        break;
    }
    case 0x3a: { t.ip = r.p; c.push(c.pop() == 0); break; }
    case 0x3b: {
        unsigned mode = r.u8();
        Word dest = pc + static_cast<Word>(static_cast<std::int32_t>(s16(r.u16())));
        t.ip = r.p;
        Word a = c.pop(), b = c.pop();
        bool conds[10] = {b == a, b != a, s32(b) < s32(a), s32(b) <= s32(a), s32(a) <= s32(b),
                          s32(a) < s32(b), (a & b) != 0, a || b, a && b, b || a};
        if ((mode & 15) >= 10) { c.fail(Errc::Unsupported, "unsupported compare mode"); break; }
        bool cond = conds[mode & 15];
        if (!(mode & 0x80)) cond = !cond;
        if (trace_panel_enabled() && t.id == 20 &&
            ((pc >= 0xB700 && pc < 0xB900) || (pc >= 0xC2C0 && pc < 0xC340) || (pc >= 0xC500 && pc < 0xC900) || (pc >= 0xEA7E && pc <= 0xEE8D) || (pc >= 0xF00 && pc < 0xF80)))
            std::fprintf(stderr, "VM-BRANCH t%u pc=%X mode=%02X lhs=%X rhs=%X cond=%d target=%X\n", t.id, pc, mode, b, a, cond, cond ? dest : t.ip);
        if (cond) t.ip = dest;
        break;
    }
    case 0x3c: {
        auto shift = static_cast<std::int8_t>(r.u8()); t.ip = r.p;
        c.push(shift_value(s32(c.pop()), shift));
        break;
    }
    case 0x40: {
        t.ip = r.p;
        Word a = c.pop(), b = c.pop(), cond = c.pop();
        c.push(cond ? b : a);
        break;
    }
    // ---- maths (verified against the original handlers, analysis/主opcode语义.md); angles are 16.16 degrees ----
    case 0x42: { // (a, b, c): a * b / c in 64 bits, truncated toward zero
        t.ip = r.p;
        const std::int64_t cc = s32(c.pop()), bb = s32(c.pop()), aa = s32(c.pop());
        if (cc == 0) { c.fail(Errc::Range, "division by zero (op 42)"); break; }
        c.push(static_cast<Word>(aa * bb / cc));
        break;
    }
    case 0x43: { // (x, y): the angle of the vector in [0, 360) degrees; the zero vector gives 0
        t.ip = r.p;
        const double y = s32(c.pop()), x = s32(c.pop());
        double deg = (x == 0 && y == 0) ? 0 : std::atan2(y, x) * 180.0 / 3.14159265358979323846;
        if (deg < 0) deg += 360.0;
        c.push(static_cast<Word>(static_cast<std::int32_t>(deg * 65536.0)));
        break;
    }
    case 0x44: { // (a, b, c): the length of the vector, truncated
        t.ip = r.p;
        const double z = s32(c.pop()), y = s32(c.pop()), x = s32(c.pop());
        c.push(static_cast<Word>(static_cast<std::int32_t>(std::sqrt(x * x + y * y + z * z))));
        break;
    }
    case 0x45: { // (destination, source, x angle, y angle, z angle): rotates a vector of three int32 values
        t.ip = r.p;
        constexpr double kRad = 3.14159265358979323846 / 180.0 / 65536.0;
        const double az = s32(c.pop()) * kRad, ay = s32(c.pop()) * kRad, ax = s32(c.pop()) * kRad;
        const Word src = c.pop(), dst = c.pop();
        if (!c.ok()) break;
        const double x = s32(c.read(src, 4, true)), y = s32(c.read(src + 4, 4, true)), z = s32(c.read(src + 8, 4, true));
        const double y1 = std::cos(ax) * y - std::sin(ax) * z, z1 = std::cos(ax) * z + std::sin(ax) * y;
        const double x2 = std::cos(ay) * x + std::sin(ay) * z1, z2 = std::cos(ay) * z1 - std::sin(ay) * x;
        const double x3 = std::cos(az) * x2 - std::sin(az) * y1, y3 = std::cos(az) * y1 + std::sin(az) * x2;
        c.write(dst, static_cast<Word>(static_cast<std::int32_t>(std::floor(x3 + 0.5))), 4);
        c.write(dst + 4, static_cast<Word>(static_cast<std::int32_t>(std::floor(y3 + 0.5))), 4);
        c.write(dst + 8, static_cast<Word>(static_cast<std::int32_t>(std::floor(z2 + 0.5))), 4);
        break;
    }
    case 0x46:   // (a, b, numerator, denominator): a + (b - a) * clamp(numerator / denominator), as 16.16
    case 0x47: { // (a, b, ratio): the same with 16.16 inputs
        t.ip = r.p;
        float ratio, a, b;
        if (op == 0x46) {
            const float den = static_cast<float>(s32(c.pop())), num = static_cast<float>(s32(c.pop()));
            ratio = den == 0 ? 0.f : num / den;
            b = static_cast<float>(s32(c.pop())), a = static_cast<float>(s32(c.pop()));
        } else {
            ratio = static_cast<float>(s32(c.pop())) / 65536.f;
            b = static_cast<float>(s32(c.pop())) / 65536.f, a = static_cast<float>(s32(c.pop())) / 65536.f;
        }
        ratio = std::min(1.f, std::max(0.f, ratio));
        const float v = (a + (b - a) * ratio) * 65536.f;
        std::int64_t q64 = static_cast<std::int64_t>(std::nearbyintf(v));
        auto q = static_cast<Word>(q64);
        if (v >= 2147483648.f || v < -2147483648.f) q = 0x80000000u;
        const Word base = q & 0xFFFF0000u, frac = q - base;
        if (frac < 64) q = base;
        else if (frac > 0xFFC0) q = base + 0x10000;
        c.push(q);
        break;
    }
    case 0x48:
    case 0x49: { // sin / cos of a 16.16 degree angle, as 16.16
        t.ip = r.p;
        const double rad = s32(c.pop()) / 65536.0 * 3.14159265358979323846 / 180.0;
        const double v = (op == 0x48 ? std::sin(rad) : std::cos(rad)) * 65536.0;
        c.push(static_cast<Word>(static_cast<std::int32_t>(v)));
        break;
    }
    case 0x60: {
        t.ip = r.p;
        Word n = c.pop(), src = c.pop(), dst = c.pop();
        if (n > kMaxBlock) { c.fail(Errc::Range, "copy too large"); break; }
        std::vector<std::uint8_t> buf;
        c.read_bytes(src, n, buf);
        c.write_bytes(dst, buf.data(), buf.size());
        break;
    }
    case 0x61: {
        t.ip = r.p;
        Word n = c.pop(), dst = c.pop();
        if (n > kMaxBlock) { c.fail(Errc::Range, "clear too large"); break; }
        std::vector<std::uint8_t> buf(n, 0);
        c.write_bytes(dst, buf.data(), n);
        break;
    }
    case 0x62: {
        t.ip = r.p;
        Word val = c.pop(), n = c.pop(), dst = c.pop();
        if (n > kMaxBlock) { c.fail(Errc::Range, "fill too large"); break; }
        std::vector<std::uint8_t> buf(n, static_cast<std::uint8_t>(val));
        c.write_bytes(dst, buf.data(), n);
        break;
    }
    case 0x63: {
        t.ip = r.p;
        Word n = c.pop(), a = c.pop(), b = c.pop();
        if (n > kMaxBlock) { c.fail(Errc::Range, "comparison too large"); break; }
        std::vector<std::uint8_t> x, y;
        c.read_bytes(a, n, x);
        c.read_bytes(b, n, y);
        c.push(x == y);
        break;
    }
    case 0x67: {
        t.ip = r.p;
        std::string repl = c.cstring(c.pop()), needle = c.cstring(c.pop()), src = c.cstring(c.pop());
        Word dst = c.pop();
        if (!c.ok()) break;
        if (needle.empty()) { c.fail(Errc::Unsupported, "empty replacement needle"); break; }
        std::string out;
        Word count = 0;
        for (std::size_t i = 0; i < src.size();) {
            if (src.compare(i, needle.size(), needle) == 0) {
                out += repl;
                i += needle.size();
                ++count;
            } else out.push_back(src[i++]);
        }
        c.write_cstring(dst, out);
        c.push(count);
        break;
    }
    case 0x68: { t.ip = r.p; c.push(static_cast<Word>(c.cstring(c.pop()).size())); break; }
    case 0x69: {
        t.ip = r.p;
        std::string a = c.cstring(c.pop()), b = c.cstring(c.pop());
        c.push(a == b);
        break;
    }
    case 0x6a: {
        t.ip = r.p;
        Word src = c.pop(), dst = c.pop();
        std::string s = c.cstring(src);
        c.write_cstring(dst, s);
        break;
    }
    case 0x6b: {
        t.ip = r.p;
        std::string tail = c.cstring(c.pop()), head = c.cstring(c.pop());
        Word dst = c.pop();
        c.write_cstring(dst, head + tail);
        break;
    }
    case 0x64: { // (destination, block size, count, source): the source block repeated `count` times
        t.ip = r.p;
        Word src = c.pop(), count = c.pop(), block = c.pop(), dst = c.pop();
        if (!c.ok()) break;
        if (std::uint64_t(block) * count > kMaxBlock) { c.fail(Errc::Range, "repeat copy too large"); break; }
        std::vector<std::uint8_t> one, all;
        c.read_bytes(src, block, one);
        for (Word i = 0; i < count; ++i) all.insert(all.end(), one.begin(), one.end());
        c.write_bytes(dst, all.data(), all.size());
        break;
    }
    case 0x65: { // (table, record width, record count, key): index of the first record equal to the key, or -1
        t.ip = r.p;
        Word key = c.pop(), count = c.pop(), width = c.pop(), table = c.pop();
        if (!c.ok()) break;
        if (std::uint64_t(width) * count > kMaxBlock) { c.fail(Errc::Range, "record search too large"); break; }
        std::vector<std::uint8_t> k, recs;
        c.read_bytes(key, width, k);
        c.read_bytes(table, std::size_t(width) * count, recs);
        Word found = 0xFFFFFFFFu;
        for (Word i = 0; i < count && width != 0 && c.ok(); ++i)
            if (std::equal(k.begin(), k.end(), recs.begin() + std::size_t(i) * width)) { found = i; break; }
        c.push(found);
        break;
    }
    case 0x66: { // (source, needle): byte offset of the first match, or -1
        t.ip = r.p;
        std::string needle = c.cstring(c.pop()), src = c.cstring(c.pop());
        std::size_t at = src.find(needle);
        c.push(at == std::string::npos ? 0xFFFFFFFFu : static_cast<Word>(at));
        break;
    }
    case 0x6c: { // (string): pushes bytes used, character value (Unicode), width class, punctuation class of the first UTF-8 character
        // verified in the original: width 1 for full-width forms, punctuation 1 for closing marks and stops (not opening brackets)
        t.ip = r.p;
        std::string s = c.cstring(c.pop());
        if (!c.ok()) break;
        const auto b0 = s.empty() ? 0u : static_cast<std::uint8_t>(s[0]);
        unsigned len = b0 < 0x80 ? 1 : b0 >= 0xF0 ? 4 : b0 >= 0xE0 ? 3 : b0 >= 0xC0 ? 2 : 1;
        if (s.empty()) len = 0;
        if (len > s.size()) len = static_cast<unsigned>(s.size());
        unsigned ch = len == 1 ? b0 : len == 2 ? b0 & 0x1F : len == 3 ? b0 & 0x0F : b0 & 0x07;
        for (unsigned i = 1; i < len; ++i) ch = (ch << 6) | (static_cast<std::uint8_t>(s[i]) & 0x3F);
        static constexpr unsigned kClosing[] = {0x3001, 0x3002, 0xFF0C, 0xFF0E, 0xFF01, 0xFF1F, 0xFF09, 0x300D, 0x300F, 0x3011, 0x3015, 0xFF3D};
        const bool wide = ch >= 0x100 && !(ch >= 0xFF61 && ch <= 0xFF9F);
        const bool punct = (ch != 0 && ch < 0x80 && std::strchr("!),.:;?]}", static_cast<int>(ch)) != nullptr) ||
                           std::find(std::begin(kClosing), std::end(kClosing), ch) != std::end(kClosing);
        c.push(len), c.push(ch), c.push(wide), c.push(punct);
        break;
    }
    case 0x6d: { // (string): ASCII letters to lower case in place
        t.ip = r.p;
        Word addr = c.pop();
        std::string s = c.cstring(addr);
        if (!c.ok()) break;
        for (char& ch : s)
            if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
        c.write_cstring(addr, s);
        break;
    }
    case 0x6e: { // (destination, source, wrap character): the source wrapped in the character, then a NUL
        t.ip = r.p;
        Word wrap = c.pop(), src = c.pop(), dst = c.pop();
        std::string s = c.cstring(src);
        if (!c.ok()) break;
        std::string out;
        out += static_cast<char>(wrap);
        out += s;
        out += static_cast<char>(wrap);
        out += '\0';
        c.write_bytes(dst, reinterpret_cast<const std::uint8_t*>(out.data()), out.size());
        break;
    }
    case 0x73: { // three strings: writes the install folder to the registry; the virtual registry always accepts it
        t.ip = r.p;
        c.pop(), c.pop(), c.pop();
        c.push(1);
        break;
    }
    case 0x77: { t.ip = r.p; c.pop(); c.push(0); break; }
    case 0x78: { // (message, mode): a yes/no box; nobody answers it here, so the answer is "no"
        t.ip = r.p;
        c.pop(), c.pop();
        c.push(0);
        break;
    }
    case 0x79: { // (message): the script's own error report ends the thread
        t.ip = r.p;
        c.fail(Errc::Unsupported, "script error: " + c.cstring(c.pop()));
        break;
    }
    case 0x7a: {
        t.ip = r.p;
        Word v = c.pop();
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d (0x%X)", static_cast<std::int32_t>(v), v);
        c.fail(Errc::Unsupported, std::string("script error value ") + buf);
        break;
    }
    case 0x7b: { // (title, data, length): a dump of script memory with a title; ends the thread
        t.ip = r.p;
        Word len = c.pop(), data = c.pop();
        std::string title = c.cstring(c.pop());
        c.fail(Errc::Unsupported, "script dump \"" + title + "\" of " + std::to_string(len) + " bytes at " + std::to_string(data));
        break;
    }
    case 0x7c: { t.ip = r.p; c.pop(), c.pop(); c.push(0); break; }
    case 0x7d: { t.ip = r.p; for (int i = 0; i < 7; ++i) c.pop(); break; }
    case 0x7e: { t.ip = r.p; c.pop(); c.push(1); break; } // text to the clipboard: success
    case 0x6f: {
        t.ip = r.p;
        std::string fmt = c.cstring(c.pop());
        Word dst = c.pop();
        std::string text = format_script(c, fmt);
        c.write_cstring(dst, text);
        break;
    }
    case 0xff: {
        unsigned ext = r.u8(); t.ip = r.p;
        if (ext == 0xf0) {
            std::string member = c.cstring(c.pop()), archive = c.cstring(c.pop());
            Word slot = c.pop();
            if (c.ok() && slot > 0xef) c.fail(Errc::Range, "extension id out of range");
            if (c.ok()) extensions[slot] = {archive, member};
        } else if (ext == 0xf1) {
            extensions.erase(c.pop());
        } else if (ext == 0xf8) {
            for (int i = 0; i < 2 && c.ok(); ++i)
                if (auto u = unload_module(t); !u) c.fail(u.error().code, u.error().message);
            if (!c.ok()) break;
            t.sp -= 4;
            Word dest = static_cast<Word>(c.read(make_addr(Tag::Stack, t.sp), 4, false));
            if (t.calls.empty()) { c.fail(Errc::Format, "extension return without call record"); break; }
            Thread::Call call = t.calls.back();
            t.calls.pop_back();
            if (c.ok() && dest != call.ret) c.fail(Errc::Format, "extension return mismatch");
            t.ip = dest;
        } else if (ext < 0xf0 && extensions.count(ext)) {
            auto [archive, name] = extensions[ext];
            // Mediator: push ip+16; call; ext F8 -- then the target block follows it directly.
            static constexpr std::uint8_t kMediator[16] = {0x06, 0x10, 0x00, 0x16, 0xff, 0xf8};
            Word start = static_cast<Word>(t.program.size());
            if (static_cast<std::uint64_t>(start) + 16 > t.config.program_capacity) {
                c.fail(Errc::Range, "program capacity exceeded by extension mediator");
                break;
            }
            t.program.insert(t.program.end(), kMediator, kMediator + 16);
            char hexid[4];
            std::snprintf(hexid, sizeof hexid, "%02X", ext);
            t.modules.push_back({start, 16, "[mediation]", hexid});
            auto target = load_module(t, archive, name);
            if (!target) { c.fail(target.error().code, target.error().message); break; }
            c.write(make_addr(Tag::Stack, t.sp), t.ip, 4);
            t.sp += 4;
            t.calls.push_back({pc, t.ip});
            t.ip = start;
        } else {
            c.fail(Errc::Unsupported, "extension " + std::to_string(ext) + " is not registered");
        }
        break;
    }
    default: {
        char buf[48];
        std::snprintf(buf, sizeof buf, "main opcode %02X is not implemented", op);
        c.fail(Errc::Unsupported, buf);
        break;
    }
    }

    if (!c.ok()) {
        char where[64];
        std::snprintf(where, sizeof where, " (op %02X at 0x%X)", op, pc);
        return fail(c.error->code, c.error->message + where);
    }
    if (trace_panel_enabled() && (trace_panel_thread || trace_message_writer))
        std::fprintf(stderr, "VM-STEP-END t%u pc=%X next=%X sp=%X\n", t.id, pc, t.ip, t.sp);
    return rc;
}

} // namespace obgi::vm
