#include "bindings/message_subops.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include "text/encoding.hpp"
#include "text/layout.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

constexpr Word kHandleBase = 0xB0000000u;
constexpr Word kPlaneSlotBase = 0x10000u; // the window object's picture (see plane_subops.cpp)
constexpr Word kTextSlotBase = 0x20000u;  // its text layer
constexpr Word kTempSlot = 0x7FFF0001u;   // a rendered string on its way into the text layer

struct Cursor {
    std::int32_t x = 0, y = 0;
    bool placed = false; // false: at the start of the text area
};

struct Shared {
    std::array<Cursor, 16> cursors;
};

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, std::function<int(Ctx&)> fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

SystemState::Plane* plane_of(SystemState& s, Ctx& c, Word handle, std::size_t* index = nullptr) {
    const Word slot = handle & 0xFFFFFFu;
    if ((handle & 0xFF000000u) == kHandleBase && slot < s.planes.size() && s.planes[slot]) {
        if (index) *index = slot;
        return &*s.planes[slot];
    }
    c.fail(Errc::Range, "not a window object handle: " + std::to_string(handle));
    return nullptr;
}

// The window's picture again: the frame bitmap, then the text layer over it.
void recompose(SystemState& s, std::size_t idx) {
    Display* d = s.display;
    if (!d || !s.planes[idx]) return;
    const auto& p = *s.planes[idx];
    const Word plane_slot = kPlaneSlotBase + static_cast<Word>(idx), text_slot = kTextSlotBase + static_cast<Word>(idx);
    if (!d->slot(plane_slot)) return;
    (void)d->fill_slot(plane_slot, 0);
    if (p.image != 0xFFFFFFFFu && p.image < 0x4000 && d->slot(p.image))
        (void)d->blit(plane_slot, 0, 0, p.image, 0, 0, p.width, p.height, 0x80, 0);
    if (d->slot(text_slot)) (void)d->blit(plane_slot, 0, 0, text_slot, 0, 0, p.width, p.height, 1, 0);
}

Word text_layer(SystemState& s, std::size_t idx) {
    Display* d = s.display;
    const Word text_slot = kTextSlotBase + static_cast<Word>(idx);
    if (d && !d->slot(text_slot) && s.planes[idx]) {
        if (d->create_slot(text_slot, s.planes[idx]->width, s.planes[idx]->height, SlotFormat::Bgra)) (void)d->fill_slot(text_slot, 0);
    }
    return text_slot;
}

// What the click that ends a message looks like: Enter, Space, the left button.
std::uint32_t decide_count(const SystemState& s) { return s.input_counts[0x0D] + s.input_counts[0x20] + s.input_counts[0x01]; }

} // namespace

void register_message_subops(vm::SubopRegistry& r, SystemState& state, TextHost& text, FontHost& fonts) {
    SystemState* s = &state;
    TextHost* th = &text;
    FontHost* fh = &fonts;
    auto shared = std::make_shared<Shared>();

    // 91:9B (out, string, font no, size, width %, bold, proportional): writes the width in pixels of the string to `out`, pushes 0
    // (0x80000001 bad size, 0x80000003 bad font number; nothing written then). Width = the laid-out line's width, unwrapped.
    add(r, 0x919B, "measure text", 7, 1, [s, th, fh](Ctx& c) {
        c.pop(), c.pop(), c.pop(); // proportional, bold, width %: not modelled
        const Word size = c.pop(), font_no = c.pop(), str_addr = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        if (size < 4 || size > 400) { c.push(0x80000001u); return 0; }
        if (font_no >= fh->table.ids.size()) { c.push(0x80000003u); return 0; }
        const std::string str = c.cstring(str_addr);
        auto fnt = th->fonts->open(fh->table.ids[font_no], static_cast<int>(size));
        if (!fnt) { c.push(0x80000003u); return 0; }
        TextStyle style;
        style.wrap = false;
        auto img = render_text(**fnt, s->code_page == 65001 ? utf8_to_utf32(str) : cp932_to_utf32(str), style, 0);
        c.write(out, img ? img->width : 0, 4);
        c.push(0);
        return 0;
    });
    // (window, decoration, frame, bitmap): the picture of the window's frame, -1 for none
    add(r, 0x9086, "window frame picture", 4, 0, [s](Ctx& c) {
        const Word image = c.pop(), b = c.pop(), a = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        std::size_t idx = 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle, &idx)) {
            p->image = image;
            p->extra = {a, b};
            recompose(*s, idx);
        }
        return 0;
    });
    // (window): clears the text and puts the cursor back at the start of the text area
    add(r, 0x928E, "window clear text", 1, 0, [s, shared](Ctx& c) {
        const Word handle = c.pop();
        if (!c.ok()) return 0;
        std::size_t idx = 0;
        if (!plane_of(*s, c, handle, &idx)) return 0;
        if (s->display && s->display->slot(kTextSlotBase + static_cast<Word>(idx))) (void)s->display->fill_slot(kTextSlotBase + static_cast<Word>(idx), 0);
        shared->cursors[idx] = {};
        recompose(*s, idx);
        return 0;
    });
    // 91:8C (window, x, y): puts the text cursor
    add(r, 0x918C, "window text cursor", 3, 0, [s, shared](Ctx& c) {
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word handle = c.pop();
        if (!c.ok()) return 0;
        std::size_t idx = 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle, &idx)) {
            p->props[0x918C] = {static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)};
            shared->cursors[idx] = {p->area[0] + x, p->area[1] + y, true};
        }
        return 0;
    });
    // 91:8D (window): 1, then the cursor position inside the text area
    add(r, 0x918D, "window text cursor query", 1, 3, [s, shared](Ctx& c) {
        const Word handle = c.pop();
        if (!c.ok()) return 0;
        std::size_t idx = 0;
        SystemState::Plane* p = plane_of(*s, c, handle, &idx);
        if (!p) return 0;
        const Cursor& cur = shared->cursors[idx];
        c.push(1);
        c.push(cur.placed ? static_cast<Word>(cur.x - p->area[0]) : 0);
        c.push(cur.placed ? static_cast<Word>(cur.y - p->area[1]) : 0);
        return 0;
    });

    // Writes `str` at the cursor of window `handle`. Returns false after failing the context.
    auto write_text = [s, th, fh, shared](Ctx& c, Word handle, const std::string& str, Word colour) {
        std::size_t idx = 0;
        SystemState::Plane* p = plane_of(*s, c, handle, &idx);
        if (!p) return false;
        const auto font_it = p->props.find(0x9188);
        // 91:88: font number, size, width percent, bold, proportional, ruby reserve
        const std::vector<Word> font = font_it != p->props.end() ? font_it->second : std::vector<Word>{0, 24, 100, 0, 0, 0};
        const int size = font.size() > 1 && font[1] >= 4 && font[1] <= 400 ? static_cast<int>(font[1]) : 24;
        std::string name;
        if (!font.empty() && font[0] < fh->table.ids.size()) name = fh->table.ids[font[0]];
        const auto gap_it = p->props.find(0x9189);
        const int gap = gap_it != p->props.end() && !gap_it->second.empty() ? static_cast<int>(size * gap_it->second[0] / 100) : 0;
        if (std::getenv("OBGI_DBGTEXT")) {
            for (std::size_t k = 0; k < s->planes.size(); ++k)
                if (s->planes[k]) {
                    const auto& q = *s->planes[k];
                    std::fprintf(stderr, "plane %zu %ux%u at %d,%d order=%u flag=%u mode=%u image=%d extra:", k, q.width, q.height, q.x, q.y, q.order, q.flag, q.mode, static_cast<int>(q.image));
                    for (Word w : q.extra) std::fprintf(stderr, " %d", static_cast<int>(w));
                    for (auto& [id, v] : q.props) {
                        std::fprintf(stderr, " %04X:[", id);
                        for (Word w : v) std::fprintf(stderr, "%d ", static_cast<int>(w));
                        std::fprintf(stderr, "]");
                    }
                    std::fprintf(stderr, "\n");
                }
            std::fprintf(stderr, "window %X size %ux%u at %d,%d image=%d(slot %d) extra=%zu", handle, p->width, p->height, p->x, p->y, static_cast<int>(p->image),
                         s->display && p->image < 0x4000 && s->display->slot(p->image) ? 1 : 0, p->extra.size());
            for (auto& [id, v] : p->props) {
                std::fprintf(stderr, " %04X:[", id);
                for (Word w : v) std::fprintf(stderr, "%d ", static_cast<int>(w));
                std::fprintf(stderr, "]");
            }
            std::fprintf(stderr, "\n");
        }
        if (std::getenv("OBGI_DBGTEXT"))
            std::fprintf(stderr, "text win=%X font=%s size=%d gap=%d colour=%08X area=%d,%d,%d,%d str=%s\n", handle, name.c_str(), size, gap, colour,
                         p->area[0], p->area[1], p->area[2], p->area[3], str.c_str());
        auto fnt = th->fonts->open(name, size);
        if (!fnt) return true; // no font to draw with: the text just does not show
        Cursor& cur = shared->cursors[idx];
        if (!cur.placed) cur = {p->area[0], p->area[1], true};
        TextStyle style;
        style.color = colour & 0xFFFFFF;
        style.line_gap = gap;
        const std::int64_t room = std::int64_t{p->area[2]} - cur.x + 1;
        auto img = render_text(**fnt, s->code_page == 65001 ? utf8_to_utf32(str) : cp932_to_utf32(str), style, room > 0 ? static_cast<std::uint32_t>(room) : 1);
        if (!img || img->width == 0 || img->height == 0) return true;
        const Word text_slot = text_layer(*s, idx);
        if (s->display->upload_slot(kTempSlot, img->width, img->height, SlotFormat::Bgra, img->pixels)) {
            (void)s->display->blit(text_slot, cur.x, cur.y, kTempSlot, 0, 0, img->width, img->height, 1, 0);
            s->display->free_slot(kTempSlot);
        }
        const int line_h = img->lines > 0 ? static_cast<int>(img->height) / img->lines : size;
        if (img->lines > 1) cur = {p->area[0], cur.y + line_h * (img->lines - 1), true};
        else cur.x += static_cast<std::int32_t>(img->width);
        recompose(*s, idx);
        return true;
    };
    // Ends the op: the thread waits (for a click at the end of the message when asked) and receives 1 when the user hurried it.
    auto finish = [](Ctx& c, bool wait_click, SystemState* st) {
        vm::Thread* t = &c.t;
        const std::uint32_t base = decide_count(*st);
        c.t.wait.kind = vm::WaitObject::Kind::Poll;
        c.t.wait.poll = [t, st, base, wait_click]() {
            if (wait_click && decide_count(*st) == base) return false;
            t->expr[t->depth] = wait_click ? 1u : 0u;
            t->depth = t->depth + 1 < t->expr.size() ? t->depth + 1 : 0;
            return true;
        };
        return static_cast<int>(vm::StayThread);
    };

    // 90:90 (window, string, colour, instant, wait at end, allow skip)
    add(r, 0x9090, "message text", 6, 0, [write_text, finish, s](Ctx& c) {
        c.pop();
        const Word wait_end = c.pop();
        c.pop();
        const Word colour = c.pop();
        const std::string str = c.cstring(c.pop());
        const Word handle = c.pop();
        if (!c.ok() || !write_text(c, handle, str, colour)) return 0;
        return finish(c, wait_end != 0, s);
    });
    // 91:90 (window, string, colour, instant, wait at end, allow skip, allow keys, ruby on, hang, unused)
    add(r, 0x9190, "message text (rich)", 10, 0, [write_text, finish, s](Ctx& c) {
        c.pop(), c.pop(), c.pop(); // unused, hang, ruby on
        c.pop(), c.pop();          // allow keys, allow skip
        const Word wait_end = c.pop();
        c.pop();
        const Word colour = c.pop();
        const std::string str = c.cstring(c.pop());
        const Word handle = c.pop();
        if (!c.ok() || !write_text(c, handle, str, colour)) return 0;
        return finish(c, wait_end != 0, s);
    });
    // 92:90 (window, string, colour, ruby on, ruby colour, hang, shadow on, dx, dy, shadow colour, shadow level, instant,
    // wait at end, allow skip, allow keys)
    add(r, 0x9290, "message text (styled)", 15, 0, [write_text, finish, s](Ctx& c) {
        c.pop(), c.pop();            // allow keys, allow skip
        const Word wait_end = c.pop();
        c.pop();                     // instant
        for (int i = 0; i < 5; ++i) c.pop(); // shadow level, colour, dy, dx, on
        c.pop(), c.pop(), c.pop();   // hang, ruby colour, ruby on
        const Word colour = c.pop();
        const std::string str = c.cstring(c.pop());
        const Word handle = c.pop();
        if (!c.ok() || !write_text(c, handle, str, colour)) return 0;
        return finish(c, wait_end != 0, s);
    });
}

} // namespace obgi
