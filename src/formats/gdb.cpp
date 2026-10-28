#include "formats/gdb.hpp"

#include <algorithm>
#include <cstring>

namespace obgi {

Result<GdbView> gdb_parse(ByteView d) {
    static constexpr char kMagic[16] = {'B', 'U', 'R', 'I', 'K', 'O', ' ', 'G', 'D', 'B', ' ', '3', '.', '0', '0', 0};
    if (d.size() < 0x14 || std::memcmp(d.data(), kMagic, sizeof kMagic) != 0)
        return fail(Errc::Format, "not a BURIKO GDB 3.00 stream");
    GdbView v;
    v.total_size = load_u32le(d.data() + 0x10);
    if (v.total_size != d.size()) return fail(Errc::Format, "GDB length field does not match data");
    v.body = d.subspan(0x10);
    return v;
}

namespace {

constexpr char kMagic3[16] = {'B', 'U', 'R', 'I', 'K', 'O', ' ', 'G', 'D', 'B', ' ', '3', '.', '0', '0', 0};

struct Reader {
    ByteView d;
    std::size_t pos = 0;
    bool take(std::size_t n, const std::uint8_t*& out) {
        if (n > d.size() - pos) return false;
        out = d.data() + pos;
        pos += n;
        return true;
    }
    bool u32(std::uint32_t& v) {
        const std::uint8_t* p;
        if (!take(4, p)) return false;
        v = load_u32le(p);
        return true;
    }
    bool cstr(std::string& s) {
        const auto* end = static_cast<const std::uint8_t*>(std::memchr(d.data() + pos, 0, d.size() - pos));
        if (!end) return false;
        s.assign(reinterpret_cast<const char*>(d.data() + pos), end - (d.data() + pos));
        pos = std::size_t(end - d.data()) + 1;
        return true;
    }
};

void put_u32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(std::uint8_t(v >> (8 * i)));
}

} // namespace

Result<GdbData> gdb_decode(ByteView d) {
    Reader r{d};
    const std::uint8_t* p;
    if (!r.take(16, p) || std::memcmp(p, kMagic3, sizeof kMagic3) != 0) return fail(Errc::Format, "not a BURIKO GDB 3.00 stream");
    GdbData g;
    std::uint32_t total = 0, n = 0, m = 0, count = 0;
    if (!r.u32(total) || total != d.size()) return fail(Errc::Format, "GDB length field does not match data");
    if (!r.u32(g.left) || !r.u32(g.top) || !r.u32(n)) return fail(Errc::Format, "GDB header truncated");
    if (!r.take(n, p)) return fail(Errc::Format, "GDB settings block truncated");
    g.settings.assign(p, p + n);
    if (!r.u32(m) || m > GdbData::kAreaSize || !r.take(m, p)) return fail(Errc::Format, "GDB global data block invalid");
    std::copy(p, p + m, g.area.begin());
    if (!r.u32(count)) return fail(Errc::Format, "GDB key table truncated");
    for (std::uint32_t i = 0; i < count; ++i) {
        std::string s;
        if (!r.cstr(s)) return fail(Errc::Format, "GDB key table truncated");
        g.keys.push_back(std::move(s));
    }
    if (!r.u32(count)) return fail(Errc::Format, "GDB bit tables truncated");
    for (std::uint32_t i = 0; i < count; ++i) {
        GdbData::BitTable t;
        if (!r.cstr(t.name) || !r.u32(t.bits) || !r.take((std::size_t(t.bits) + 7) >> 3, p)) return fail(Errc::Format, "GDB bit tables truncated");
        t.data.assign(p, p + ((std::size_t(t.bits) + 7) >> 3));
        g.tables.push_back(std::move(t));
    }
    if (r.pos != d.size()) return fail(Errc::Format, "GDB has trailing bytes");
    return g;
}

std::vector<std::uint8_t> gdb_encode(const GdbData& g) {
    std::vector<std::uint8_t> out(kMagic3, kMagic3 + sizeof kMagic3);
    put_u32(out, 0); // total, patched below
    put_u32(out, g.left);
    put_u32(out, g.top);
    put_u32(out, std::uint32_t(g.settings.size()));
    out.insert(out.end(), g.settings.begin(), g.settings.end());
    put_u32(out, std::uint32_t(g.area.size()));
    out.insert(out.end(), g.area.begin(), g.area.end());
    put_u32(out, std::uint32_t(g.keys.size()));
    for (const auto& k : g.keys) {
        out.insert(out.end(), k.begin(), k.end());
        out.push_back(0);
    }
    put_u32(out, std::uint32_t(g.tables.size()));
    for (const auto& t : g.tables) {
        out.insert(out.end(), t.name.begin(), t.name.end());
        out.push_back(0);
        put_u32(out, t.bits);
        out.insert(out.end(), t.data.begin(), t.data.end());
    }
    const auto total = std::uint32_t(out.size());
    for (int i = 0; i < 4; ++i) out[0x10 + i] = std::uint8_t(total >> (8 * i));
    return out;
}

} // namespace obgi
