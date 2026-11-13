#include "bindings/message_subops.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <functional>
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
constexpr Word kFrameSlotBase = 0x30000u; // window-owned frame, independent of script bitmap lifetime
constexpr Word kTempSlot = 0x7FFF0001u;   // a rendered string on its way into the text layer

using Cursor = MessageService::Cursor;
using Reveal = MessageVisual;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, std::function<int(Ctx&)> fn, int deferred = 0) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn), static_cast<std::uint8_t>(deferred)});
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
    if (p.frame_visible && p.image != 0xFFFFFFFFu && d->slot(p.image))
        (void)d->blit(plane_slot, 0, 0, p.image, 0, 0, p.width, p.height, 0x80, 0);
    if (p.text_visible && d->slot(text_slot)) (void)d->blit(plane_slot, 0, 0, text_slot, 0, 0, p.width, p.height, 1, s.plane_text_level[idx]);
}

Word frame_layer(SystemState& s, std::size_t idx) {
    Display* d = s.display;
    const Word slot = kFrameSlotBase + static_cast<Word>(idx);
    if (d && !d->slot(slot) && s.planes[idx]) {
        if (d->create_slot(slot, s.planes[idx]->width, s.planes[idx]->height, SlotFormat::Bgra)) (void)d->fill_slot(slot, 0);
    }
    return slot;
}

Word text_layer(SystemState& s, std::size_t idx) {
    Display* d = s.display;
    const Word text_slot = kTextSlotBase + static_cast<Word>(idx);
    if (d && !d->slot(text_slot) && s.planes[idx]) {
        if (d->create_slot(text_slot, s.planes[idx]->width, s.planes[idx]->height, SlotFormat::Bgra)) (void)d->fill_slot(text_slot, 0);
    }
    return text_slot;
}

// Frames between two characters of a message: 90:94 gives milliseconds (a frame is 16 ms), 2 frames until a script sets it;
// OBGI_TEXT_FRAMES overrides; 0 shows messages at once.
int character_ms(const SystemState& s) {
    static const char* e = std::getenv("OBGI_TEXT_FRAMES");
    if (e) return std::max(0, std::atoi(e)) * 16;
    const auto it = s.records.find(0x9094);
    if (it == s.records.end() || it->second.empty()) return 32;
    return static_cast<int>(it->second[0]);
}

// How a message is drawn beyond its colour: the shadow (90:9C/9D globally, or 92:90's own) and the rich pacing (91:98).
struct TextOpts {
    bool rich = false;
    bool ruby = false, kinsoku = false;
    int shadow_on = 0, shadow_dx_pct = 0, shadow_dy_pct = 0, shadow_level = 0x100;
    std::uint32_t shadow_colour = 0;
};

// Explicit style shared by 92:90 (paced) and 92:91 (immediate).
TextOpts pop_styled_opts(Ctx& c) {
    TextOpts opts;
    opts.shadow_level = static_cast<int>(c.pop());
    opts.shadow_colour = c.pop();
    opts.shadow_dy_pct = static_cast<int>(c.pop());
    opts.shadow_dx_pct = static_cast<int>(c.pop());
    opts.shadow_on = static_cast<int>(c.pop());
    opts.kinsoku = c.pop() != 0;
    c.pop(); // ruby colour
    opts.ruby = c.pop() != 0;
    return opts;
}

// The shadow of the plain output and of 91:90: 90:9C switches it on, 90:9D gives offsets in percent of the font and the level.
TextOpts global_text_opts(const SystemState& s, bool rich) {
    TextOpts o;
    o.rich = rich;
    const auto on = s.records.find(0x909C);
    const auto par = s.records.find(0x909D);
    if (on != s.records.end() && !on->second.empty() && on->second[0] && par != s.records.end() && par->second.size() >= 3) {
        o.shadow_on = static_cast<int>(on->second[0]);
        o.shadow_dx_pct = static_cast<int>(par->second[0]), o.shadow_dy_pct = static_cast<int>(par->second[1]);
        o.shadow_level = static_cast<int>(par->second[2]);
    }
    return o;
}

// 91:98 (delay per character, fade steps, ...) in milliseconds; the engine starts with 25 and 30
std::pair<int, int> rich_pacing(const SystemState& s) {
    const auto it = s.records.find(0x9198);
    if (it == s.records.end() || it->second.size() < 2) return {25, 30};
    return {static_cast<int>(it->second[0]), std::max(static_cast<int>(it->second[1]), 1)};
}

// 90:97 (on, ms) and 90:9B (on, ms) as milliseconds, 0 when off or unset
std::uint32_t record_ms(const SystemState& s, std::uint16_t id) {
    const auto it = s.records.find(id);
    return it != s.records.end() && it->second.size() >= 2 && it->second[0] ? it->second[1] : 0;
}

} // namespace

void register_message_subops(vm::SubopRegistry& r, SystemState& state, TextHost& text, FontHost& fonts) {
    SystemState* s = &state;
    TextHost* th = &text;
    FontHost* fh = &fonts;
    auto shared = text.messages;

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
        auto img = render_text(**fnt, script_to_utf32(str, s->code_page), style, 0);
        c.write(out, img ? img->width : 0, 4);
        c.push(0);
        return 0;
    });
    // 92:99 (positions, count, string, font no, size, width %, bold): for every character writes three words at `positions`
    // (left bearing, advance, offset of the glyph below the line top) and the number of characters at `count`. Pushes 0, or
    // 0x80000001 / 0x80000003 for a bad size / font as 91:9B does.
    add(r, 0x9299, "measure characters", 7, 1, [s, th, fh](Ctx& c) {
        c.pop(), c.pop(); // bold, width %: not modelled
        const Word size = c.pop(), font_no = c.pop(), str_addr = c.pop(), count_out = c.pop(), positions = c.pop();
        if (!c.ok()) return 0;
        if (size < 4 || size > 400) { c.push(0x80000001u); return 0; }
        if (font_no >= fh->table.ids.size()) { c.push(0x80000003u); return 0; }
        auto fnt = th->fonts->open(fh->table.ids[font_no], static_cast<int>(size));
        if (!fnt) { c.push(0x80000003u); return 0; }
        const std::string str = c.cstring(str_addr);
        const std::u32string text = script_to_utf32(str, s->code_page);
        Word n = 0;
        for (char32_t ch : text) {
            const Glyph* g = (*fnt)->glyph(ch);
            if (!g) g = (*fnt)->glyph(U'?');
            if (positions) {
                c.write(positions + 12 * n, static_cast<Word>(g ? g->left : 0), 4);
                c.write(positions + 12 * n + 4, static_cast<Word>(g ? g->advance : 0), 4);
                c.write(positions + 12 * n + 8, static_cast<Word>(g ? (*fnt)->ascent() - g->top : 0), 4);
            }
            ++n;
        }
        if (count_out) c.write(count_out, n, 4);
        c.push(0);
        return 0;
    });
    // (window, decoration, frame, bitmap): the picture of the window's frame, -1 for none
    add(r, 0x9086, "window frame picture", 4, 0, [s](Ctx& c) {
        const Word image = c.pop(), b = c.pop(), a = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        std::size_t idx = 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle, &idx)) {
            const Word frame = frame_layer(*s, idx);
            if (s->display && s->display->slot(frame)) {
                (void)s->display->fill_slot(frame, 0);
                if (image != 0xFFFFFFFFu) {
                    const SlotInfo* info = s->display->slot(image);
                    if (!info) { c.fail(Errc::Range, "window frame bitmap does not exist"); return 0; }
                    if (auto r = s->display->blit(frame, 0, 0, image, 0, 0, info->width, info->height, 0x80, 0); !r) { c.fail(r.error().code, r.error().message); return 0; }
                }
            }
            p->image = frame;
            p->frame_visible = image != 0xFFFFFFFFu;
            p->extra = {a, b};
            recompose(*s, idx);
        }
        return 0;
    });
    for (const bool text : {false, true}) {
        add(r, text ? 0x928D : 0x9289, text ? "draw window text layer" : "draw window frame layer", 6, 0, [s, text](Ctx& c) {
            const Word level = c.pop(), effect = c.pop(), image = c.pop();
            const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
            const Word handle = c.pop();
            if (!c.ok()) return 0;
            std::size_t idx = 0;
            auto* p = plane_of(*s, c, handle, &idx);
            if (!p || !s->display) return 0;
            const SlotInfo* info = s->display->slot(image);
            if (!info || image >= 0x4000 || level > 0x100 || !blend_mode_supported(effect)) { c.fail(Errc::Range, "invalid window layer bitmap or blend"); return 0; }
            const Word width = info->width, height = info->height;
            const Word dst = text ? text_layer(*s, idx) : frame_layer(*s, idx);
            if (auto r = s->display->blit(dst, x, y, image, 0, 0, width, height, effect, level); !r) { c.fail(r.error().code, r.error().message); return 0; }
            if (!text) p->image = dst;
            recompose(*s, idx);
            return 0;
        });
        add(r, text ? 0x928C : 0x9288, text ? "show window text layer" : "show window frame layer", 2, 0, [s, text](Ctx& c) {
            const Word on = c.pop(), handle = c.pop();
            if (!c.ok()) return 0;
            std::size_t idx = 0;
            if (auto* p = plane_of(*s, c, handle, &idx)) {
                if (text) p->text_visible = on != 0; else p->frame_visible = on != 0;
                recompose(*s, idx);
            }
            return 0;
        });
    }
    add(r, 0x928A, "fill window frame layer", 2, 0, [s](Ctx& c) {
        const Word colour = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        std::size_t idx = 0;
        if (auto* p = plane_of(*s, c, handle, &idx); p && s->display) {
            const Word frame = frame_layer(*s, idx);
            if (auto r = s->display->fill_slot(frame, colour); !r) { c.fail(r.error().code, r.error().message); return 0; }
            p->image = frame;
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
    auto write_text = [s, th, fh, shared](Ctx& c, Word handle, const std::string& whole, Word colour, const TextOpts& opts, Reveal& rv) {
        std::size_t idx = 0;
        SystemState::Plane* p = plane_of(*s, c, handle, &idx);
        if (!p) return false;
        // The original's text output acts on two control bytes: 0x01 scrolls the page out and 0x0C fades it out; both then
        // clear the text layer and put the cursor home. What comes before the last one is cleared at once, the rest is typed.
        std::string str = whole;
        char control = 0;
        if (const std::size_t cut = str.find_last_of("\x01\x0C"); cut != std::string::npos) {
            control = str[cut];
            str.erase(0, cut + 1);
        }
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
            for (Word id : {0x9097u, 0x909Bu, 0x909Cu, 0x909Eu, 0x9099u, 0x909Au, 0x9094u, 0x9198u}) {
                auto it = s->records.find(id);
                std::fprintf(stderr, "rec %04X:", id);
                if (it != s->records.end())
                    for (Word w : it->second) std::fprintf(stderr, " %d", static_cast<int>(w));
                std::fprintf(stderr, "\n");
            }
        if (std::getenv("OBGI_DBGTEXT"))
            std::fprintf(stderr, "text win=%X font=%s size=%d gap=%d colour=%08X area=%d,%d,%d,%d str=%s\n", handle, name.c_str(), size, gap, colour,
                         p->area[0], p->area[1], p->area[2], p->area[3], str.c_str());
        // 0x0C fades the old page over 90:96's steps x ms, 0x01 scrolls it out line by line (90:95's steps x ms per line);
        // the new text starts from an empty layer and the cursor at home
        bool cleared = false;
        if (control && s->display) {
            cleared = true;
            const Word text_slot0 = text_layer(*s, idx);
            const Cursor old_cur = shared->cursors[idx];
            shared->cursors[idx] = {};
            auto pace = [&](std::uint16_t id) { // frames of one step group: steps x ms / 16
                const auto it = s->records.find(id);
                return it != s->records.end() && it->second.size() >= 2 ? static_cast<int>(it->second[0] * it->second[1] / 16) : 0;
            };
            const SlotInfo* si = s->display->slot(text_slot0);
            if (std::getenv("OBGI_DBGTEXT")) std::fprintf(stderr, "control %02X: fade %d scroll %d frames per group\n", control, pace(0x9096), pace(0x9095));
            if (si &&!std::getenv("OBGI_TEXT_FRAMES")) {
                const std::uint32_t tw0 = si->width, th0 = si->height;
                if (control == '\x0C') {
                    rv.pre_frames = pace(0x9096);
                    rv.pre = [s, idx](int k, int total) {
                        s->plane_text_level[idx] = static_cast<std::uint32_t>(0x100 * (k + 1) / std::max(total, 1));
                        recompose(*s, idx);
                    };
                } else if (old_cur.placed) {
                    const int line_h = std::max(size + gap, 1);
                    const int lines = (old_cur.y - p->area[1]) / line_h + (old_cur.x > p->area[0] ? 1 : 0) + 1;
                    rv.pre_frames = lines * std::max(pace(0x9095), 1);
                    auto old_px = std::make_shared<std::vector<std::uint32_t>>();
                    if (auto px = s->display->read_slot(text_slot0)) *old_px = std::move(*px);
                    rv.pre = [s, idx, text_slot0, old_px, tw0, th0, lines, line_h](int k, int total) {
                        const std::size_t off = std::size_t(lines) * line_h * (k + 1) / std::max(total, 1);
                        std::vector<std::uint32_t> moved(old_px->size(), 0);
                        if (old_px->size() == std::size_t{tw0} * th0 && off < th0)
                            std::copy(old_px->begin() + off * tw0, old_px->end(), moved.begin());
                        (void)s->display->upload_slot(text_slot0, tw0, th0, SlotFormat::Bgra, moved);
                        recompose(*s, idx);
                    };
                }
            }
            rv.pre_done = [s, idx, text_slot0]() {
                (void)s->display->fill_slot(text_slot0, 0);
                s->plane_text_level[idx] = 0;
                recompose(*s, idx);
            };
        }
        int charset = -1;
        for (const auto& info : fh->table.infos)
            if (info.name == name) { charset = info.charset; break; }
        auto fnt = th->fonts->open(name, size, charset);
        if (!fnt) return true; // no font to draw with: the text just does not show
        std::shared_ptr<Font> face = *fnt;
        Cursor& cur = shared->cursors[idx];
        if (!cur.placed) cur = {p->area[0], p->area[1], true};
        TextStyle style;
        style.color = colour & 0xFFFFFF;
        style.line_gap = gap;
        style.fixed_advance = size * (font.size() > 2 ? static_cast<int>(font[2]) : 100) / 100;
        style.row_height = size + gap;
        if (opts.shadow_on) { // offsets are percentages of the font size, at least one pixel; colour 0 = the glyph dimmed to black
            style.shadow_dx = std::max(size * opts.shadow_dx_pct / 100, 1);
            style.shadow_dy = std::max(size * opts.shadow_dy_pct / 100, 1);
            style.shadow_color = opts.shadow_colour & 0xFFFFFF;
            style.shadow_level = opts.shadow_level;
        }
        // lay out from the left edge of the text area, the first line indented to where the cursor is
        style.first_indent = cur.x - p->area[0];
        const std::int64_t room = std::int64_t{p->area[2]} - p->area[0] + 1;
        const std::uint32_t max_w = room > 0 ? static_cast<std::uint32_t>(room) : 1;
        const std::u32string text = script_to_utf32(str, s->code_page);
        int ruby_x = 0, ruby_y = 0;
        if (opts.ruby) {
            const auto it = s->records.find(0x9198);
            if (it != s->records.end() && it->second.size() >= 5) {
                ruby_x = static_cast<int>(it->second[4]);
                ruby_y = size * static_cast<int>(it->second[3]) / 100;
            }
        }
        style.first_indent += ruby_x;
        style.continuation_indent = ruby_x;
        if (opts.kinsoku) {
            style.right_margin = size * 2;
            if (!text.empty() && (text[0] == U'「' || text[0] == U'『' || text[0] == U'（' || text[0] == U'　' || text[0] == U'"'))
                style.continuation_indent += style.fixed_advance;
        }
        auto layout = std::make_shared<TextLayout>(layout_text(*face, text, style, max_w));
        if (layout->glyphs.empty()) return true;
        const auto [char_delay, fade_steps] = rich_pacing(*s);
        if (std::getenv("OBGI_DBGTEXT"))
            std::fprintf(stderr, "text opts rich=%d shadow=%d dx%%=%d dy%%=%d colour=%06X level=%d delay=%d fade=%d size=%d\n", opts.rich, opts.shadow_on,
                         opts.shadow_dx_pct, opts.shadow_dy_pct, opts.shadow_colour, opts.shadow_level, char_delay, fade_steps, size);
        const Word text_slot = text_layer(*s, idx);
        auto snapshot = std::make_shared<std::vector<std::uint32_t>>();
        std::uint32_t tw = 0, th_ = 0;
        if (const SlotInfo* si = s->display->slot(text_slot)) {
            tw = si->width, th_ = si->height;
            if (cleared) snapshot->assign(std::size_t{tw} * th_, 0); // the old page is gone before this text starts
            else if (auto px = s->display->read_slot(text_slot)) *snapshot = std::move(*px);
        }
        const std::int32_t x0 = p->area[0], y0 = cur.y + ruby_y;
        const int line_h = layout->row_height;
        const std::size_t glyphs = layout->glyphs.size();
        rv.rich = opts.rich && char_delay > 0;
        rv.total = rv.rich ? static_cast<std::size_t>(char_delay) * (glyphs - 1) + static_cast<std::size_t>(fade_steps) : glyphs;
        rv.draw = [s, idx, text_slot, layout, style, face, snapshot, tw, th_, x0, y0, rich = rv.rich, char_delay = char_delay, fade_steps = fade_steps](std::size_t k) {
            Display* d = s->display;
            if (!d || !s->planes[idx]) return;
            if (std::getenv("OBGI_TRACE_MENU")) std::fprintf(stderr, "REVEAL plane=%zu progress=%zu text_slot=%u\n", idx, k, text_slot);
            if (snapshot->size() == std::size_t{tw} * th_) (void)d->upload_slot(text_slot, tw, th_, SlotFormat::Bgra, *snapshot);
            std::vector<std::uint8_t> opacity;
            if (rich) { // k ms after the start: glyph i begins to fade in at i * delay and is solid fade_steps later
                opacity.resize(layout->glyphs.size());
                for (std::size_t i = 0; i < opacity.size(); ++i) {
                    const std::int64_t t = std::int64_t(k) - std::int64_t(i) * char_delay;
                    opacity[i] = static_cast<std::uint8_t>(t <= 0 ? 0 : t >= fade_steps ? 255 : t * 255 / fade_steps);
                }
            }
            const TextImage part = paint_text(*layout, style, rich ? layout->glyphs.size() : k, opacity);
            if (part.width == 0 || part.height == 0) return;
            if (d->upload_slot(kTempSlot, part.width, part.height, SlotFormat::Bgra, part.pixels)) {
                (void)d->blit(text_slot, x0, y0, kTempSlot, 0, 0, part.width, part.height, 1, 0);
                d->free_slot(kTempSlot);
            }
            recompose(*s, idx);
        };
        cur = {p->area[0] + layout->last_width, cur.y + line_h * (layout->lines - 1), true};
        return true;
    };
    // Ends the op: the thread types the message out (a click shows the rest at once), then waits for a click at the end when
    // asked, and receives 1 when the user ended it that way.
    auto finish = [shared](Ctx& c, Reveal visual, bool instant, bool allow_skip, bool wait_click, SystemState* state) {
        MessagePacing pacing;
        pacing.character_ms = character_ms(*state);
        pacing.auto_ms = record_ms(*state, 0x9097);
        pacing.delay_ms = record_ms(*state, 0x909B);
        const auto automatic = state->records.find(0x9097);
        pacing.automatic = automatic != state->records.end() && !automatic->second.empty() && automatic->second[0] != 0;
        pacing.instant = instant;
        pacing.allow_skip = allow_skip;
        pacing.wait_click = wait_click;
        MessageInput input(*state);
        auto operation = shared->start(std::move(visual), pacing, c.vm.now_ms, 0);
        vm::WaitObject wait;
        wait.kind = vm::WaitObject::Kind::Poll;
        wait.poll = [operation = std::move(operation), state, input](const vm::WaitContext& context) mutable -> Result<vm::WaitUpdate> {
            // 80:4C message 0 interrupts the active wait (e.g. a backlog jump).
            if (context.notified) return vm::WaitUpdate::complete({0});
            operation.pacing.visual_interval_us = std::max(std::max(1u, state->frame_ms) * 1000u, state->presentation_interval_us);
            auto result = operation.update(context.now_ms, input.poll(*state));
            return result ? vm::WaitUpdate::complete({*result}) : vm::WaitUpdate::pending();
        };
        if (wait_click) {
            ++state->message_wait_clicks;
            wait.cleanup = [state]() {
                if (state->message_wait_clicks) --state->message_wait_clicks;
            };
        }
        c.suspend(std::move(wait), 1);
        return static_cast<int>(vm::StayThread);
    };

    // 90:90 (window, string, colour, instant, wait at end, allow skip)
    add(r, 0x9090, "message text", 6, 1, [write_text, finish, s](Ctx& c) {
        const Word skip = c.pop();
        const Word wait_end = c.pop();
        const Word instant = c.pop();
        const Word colour = c.pop();
        const std::string str = c.cstring(c.pop());
        const Word handle = c.pop();
        Reveal rv;
        if (!c.ok() || !write_text(c, handle, str, colour, global_text_opts(*s, false), rv)) return 0;
        return finish(c, rv, instant != 0, skip != 0, wait_end != 0, s);
    }, 1);
    // 91:90 (window, string, colour, instant, wait at end, allow skip, allow keys, ruby on, hang, unused)
    add(r, 0x9190, "message text (rich)", 10, 1, [write_text, finish, s](Ctx& c) {
        c.pop(); // unused
        const bool hang = c.pop() != 0, ruby = c.pop() != 0;
        c.pop();                   // allow keys
        const Word skip = c.pop();
        const Word wait_end = c.pop();
        const Word instant = c.pop();
        const Word colour = c.pop();
        const std::string str = c.cstring(c.pop());
        const Word handle = c.pop();
        Reveal rv;
        auto opts = global_text_opts(*s, true); opts.ruby = ruby; opts.kinsoku = hang;
        if (!c.ok() || !write_text(c, handle, str, colour, opts, rv)) return 0;
        return finish(c, rv, instant != 0, skip != 0, wait_end != 0, s);
    }, 1);
    // 92:90 (window, string, colour, ruby on, ruby colour, hang, shadow on, dx, dy, shadow colour, shadow level, instant,
    // wait at end, allow skip, allow keys)
    add(r, 0x9290, "message text (styled)", 15, 1, [write_text, finish, s](Ctx& c) {
        c.pop();                     // allow keys
        const Word skip = c.pop();
        const Word wait_end = c.pop();
        const Word instant = c.pop();
        auto opts = pop_styled_opts(c);
        opts.rich = true;
        const Word colour = c.pop();
        const std::string str = c.cstring(c.pop());
        const Word handle = c.pop();
        Reveal rv;
        if (!c.ok() || !write_text(c, handle, str, colour, opts, rv)) return 0;
        return finish(c, rv, instant != 0, skip != 0, wait_end != 0, s);
    }, 1);
    // 92:91 (window, string, colour, ruby on, ruby colour, hang, shadow on, dx, dy, shadow colour, shadow level).
    // Native 00495170 calls the synchronous window renderer, consumes 11 arguments and pushes nothing.
    add(r, 0x9291, "draw window text (styled)", 11, 0, [write_text](Ctx& c) {
        const auto opts = pop_styled_opts(c);
        const Word colour = c.pop();
        const std::string str = c.cstring(c.pop());
        const Word handle = c.pop();
        Reveal rv;
        if (!c.ok() || !write_text(c, handle, str, colour, opts, rv)) return 0;
        if (rv.pre_done) rv.pre_done();
        if (rv.draw) rv.draw(rv.total);
        return 0;
    });
}

} // namespace obgi
