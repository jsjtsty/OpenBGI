#include <cstdio>
#include <cstdlib>
#include "bindings/font_subops.hpp"

#include <algorithm>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

FontTable::Info* find_info(FontTable& t, const std::string& name) {
    for (auto& i : t.infos)
        if (i.name == name) return &i;
    return nullptr;
}

// Font names a script might look for; the ones the host can really load are what B0:C5 lists.
constexpr const char* kKnownFonts[] = {"MS Gothic", "MS Mincho", "Meiryo", "Yu Gothic"};

// Script address 0 means "no string"
std::optional<std::string> optional_string(Ctx& c, Word addr) {
    if (addr == 0) return std::nullopt;
    return c.cstring(addr);
}

} // namespace

void register_font_subops(vm::SubopRegistry& r, FontHost& host) {
    FontHost* h = &host;

    // (name, charset): the id of a font name, registering it first when new
    add(r, 0xB0C1, "register font name", 2, 1, [h](Ctx& c) {
        const Word charset = c.pop();
        std::string name = c.cstring(c.pop());
        if (!c.ok()) return 0;
        FontTable& t = h->table;
        static constexpr int kWinCharset[4] = {0x80, 0, 0x86, 0x88};
        if (charset < 4) {
            if (auto* info = find_info(t, name)) info->charset = kWinCharset[charset];
            else t.infos.insert(t.infos.begin(), {name, std::nullopt, kWinCharset[charset]});
        }
        auto it = std::find(t.ids.begin(), t.ids.end(), name);
        if (it == t.ids.end()) it = t.ids.insert(t.ids.end(), name);
        c.push(static_cast<Word>(it - t.ids.begin()));
        return 0;
    });
    // (destination, charset): writes the installed font names, NUL separated, to the destination (0 = just count).
    // The original counts what Windows enumerates for the charset; the count is pushed (the original's result is unclear).
    add(r, 0xB0C5, "list installed fonts", 2, 1, [h](Ctx& c) {
        const Word charset = c.pop(); // every font here is available in every charset
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "B0:C5 charset=%X\n", charset);
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        std::vector<std::uint8_t> bytes;
        Word count = 0;
        for (const char* name : kKnownFonts) {
            if (!h->fonts || !h->fonts->has_exact(name)) continue;
            ++count;
            bytes.insert(bytes.end(), name, name + std::char_traits<char>::length(name) + 1);
        }
        // verified against the original: a zero destination only measures (pushes the byte size, NULs included);
        // otherwise the names are written and the count is pushed
        if (dst == 0) {
            c.push(static_cast<Word>(bytes.size()));
            return 0;
        }
        if (!bytes.empty()) c.write_bytes(dst, bytes.data(), bytes.size());
        c.push(count);
        return 0;
    });
    // (destination, name): 1 when the font is installed; the destination then receives its pitch class (2 = variable)
    add(r, 0xB0C6, "look up font", 2, 1, [h](Ctx& c) {
        std::string name = c.cstring(c.pop());
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        const bool found = h->fonts && h->fonts->has_exact(name);
        if (found && dst != 0) c.write(dst, 2, 4);
        c.push(found ? 1 : 0);
        return 0;
    });
    // (font, alias): gives `font` an alias name; a missing font (address 0) sets the default alias instead
    add(r, 0xB0C7, "set font alias", 2, 0, [h](Ctx& c) {
        const Word alias_addr = c.pop(), font_addr = c.pop();
        auto alias = optional_string(c, alias_addr);
        auto font = optional_string(c, font_addr);
        if (!c.ok()) return 0;
        FontTable& t = h->table;
        if (!font) {
            t.default_alias = alias;
        } else if (auto* info = find_info(t, *font)) {
            info->alias = alias;
        } else {
            t.infos.insert(t.infos.begin(), {*font, alias, -1});
        }
        return 0;
    });
    // (name, scale x, scale y, offset x, offset y) popped from the right. Scales are 0.16 .. 2.0 in 16.16 fixed point
    // (all four zero = unscaled); offsets are bounded by 65536 - 2^32 / scale.
    add(r, 0x910E, "define font", 5, 0, [h](Ctx& c) {
        const auto offset_y = static_cast<std::int32_t>(c.pop());
        const auto offset_x = static_cast<std::int32_t>(c.pop());
        const Word scale_y = c.pop(), scale_x = c.pop();
        std::string name = c.cstring(c.pop());
        if (!c.ok()) return 0;
        if (!(scale_x == 0 && scale_y == 0 && offset_x == 0 && offset_y == 0)) {
            if (scale_x != 0 && (scale_x < 0x10000 || scale_x > 0x20000)) {
                c.fail(Errc::Range, "font scale x " + std::to_string(scale_x) + " out of range");
                return 0;
            }
            if (scale_y - 0x10000u > 0x10000u) {
                c.fail(Errc::Range, "font scale y " + std::to_string(scale_y) + " out of range");
                return 0;
            }
            const double sx = scale_x ? double(scale_x) : double(scale_y);
            if (static_cast<std::int32_t>(65536.0 - 4294967296.0 / sx) < offset_x ||
                static_cast<std::int32_t>(65536.0 - 4294967296.0 / double(scale_y)) < offset_y) {
                c.fail(Errc::Range, "font offset out of range for font \"" + name + "\"");
                return 0;
            }
        }
        h->table.definitions[name] = {scale_x, scale_y, offset_x, offset_y};
        return 0;
    });
}

} // namespace obgi
