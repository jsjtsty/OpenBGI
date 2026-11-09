// Script window planes (90:80..89). The original keeps up to 16 window objects (FUN_004486d0) and hands the scripts
// the handle 0xB0000000 + slot. Nothing draws them yet; the state a display object needs later is kept in
// SystemState::planes. Analysis: analysis/子opcode语义表.md and the handlers FUN_0048cdb0..0048d240.
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdio>
#include <cstdlib>

#include "bindings/panel.hpp"
#include "render/background_fx.hpp"
#include "bindings/system_subops.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

constexpr Word kHandleBase = 0xB0000000u;
constexpr Word kPlaneSlotBase = 0x10000u; // image slots above the scripts' own range (0..0x3fff)

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn, int deferred = 0) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn), static_cast<std::uint8_t>(deferred)});
}

// the plane a handle names, or a script error
SystemState::Plane* plane_of(SystemState& s, Ctx& c, Word handle) {
    const Word slot = handle & 0xFFFFFFu;
    if ((handle & 0xFF000000u) == kHandleBase && slot < s.planes.size() && s.planes[slot]) return &*s.planes[slot];
    c.fail(Errc::Range, "not a window object handle: " + std::to_string(handle));
    return nullptr;
}

// OBGI_DBGOBJ=<hex object id>: prints every recorded object op aimed at that object
void debug_object_op(std::uint16_t id, const std::vector<Word>& a) {
    static const char* env = std::getenv("OBGI_DBGOBJ");
    if (!env || a.empty() || a[0] != static_cast<Word>(std::strtoul(env, nullptr, 16))) return;
    std::fprintf(stderr, "op %04X:", id);
    for (Word w : a) std::fprintf(stderr, " %d", static_cast<int>(w));
    std::fprintf(stderr, "\n");
}

// Display object settings 91:31..4A, 91:55 (FUN_00490810..004912E0): the numbers are kept per op until the compositor
// reads them; only master/slave binding (91:3E, 91:3F) is modelled and checked.
void register_object_settings(vm::SubopRegistry& r, SystemState& state, Display* display) {
    SystemState* s = &state;
    struct Rec { std::uint16_t id; const char* name; int pops, pushes; };
    static const Rec recs[] = {{0x9140, "object stretch 91:40", 9, 0}, {0x9141, "object setting 91:41", 1, 0},
                               {0x9142, "object setting 91:42", 2, 0}, {0x9143, "object setting 91:43", 3, 0},
                               {0x9144, "object setting 91:44", 2, 0}, {0x9145, "object setting 91:45", 2, 0},
                               {0x9146, "object setting 91:46", 4, 0}, {0x9147, "object setting 91:47", 5, 0},
                               {0x9148, "object layer 91:48", 3, 0}, {0x9149, "object layer 91:49", 3, 0},
                               {0x914A, "object layer 91:4A", 4, 0}, {0x9155, "object query 91:55", 2, 1},
                               {0x913D, "object query 91:3D", 1, 1}};
    static const Rec loose[] = {{0x9053, "object setting 90:53", 5, 0}, {0x9055, "object setting 90:55", 2, 0}};
    // 90:3C (object, bmp): the object's hit-test mask, built from the opaque pixels of bitmap `bmp` (alpha of an ARGB picture, any colour bit of
    // an RGB one, non-zero gray); -1 removes it (the whole picture hits), -2 installs a mask that never hits (gfx/mgr/generic.c)
    add(r, 0x903C, "set object hit mask", 2, 0, [display](Ctx& c) {
        const auto bmp = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok() || !display) return 0;
        Sprite* sp = display->find_sprite(h);
        if (!sp) { c.fail(Errc::Range, "no display object " + std::to_string(h)); return 0; }
        sp->hit_bits.clear(), sp->hit_w = sp->hit_h = 0, sp->has_hit_mask = false;
        if (bmp == -1) return 0;
        std::uint32_t w = 0, hh = 0;
        std::vector<std::uint32_t> px;
        SlotFormat format = SlotFormat::Bgra;
        if (bmp == -2) {
            const SlotInfo* own = display->slot(sp->slot);
            w = own ? own->width : 0, hh = own ? own->height : 0;
            px.assign(std::size_t{w} * hh, 0);
        } else {
            const SlotInfo* si = bmp >= 0 && bmp < 0x4000 ? display->slot(static_cast<Word>(bmp)) : nullptr;
            if (!si) { c.fail(Errc::Range, "invalid bitmap " + std::to_string(bmp)); return 0; }
            w = si->width, hh = si->height, format = si->format;
            auto data = display->read_slot(static_cast<Word>(bmp));
            if (!data) { c.fail(data.error().code, data.error().message); return 0; }
            px = std::move(*data);
        }
        sp->hit_w = w, sp->hit_h = hh, sp->has_hit_mask = true;
        const std::uint32_t pitch = (w + 7) >> 3;
        sp->hit_bits.assign(std::size_t{pitch} * hh + 1, 0);
        for (std::uint32_t y = 0; y < hh; ++y)
            for (std::uint32_t x = 0; x < w; ++x) {
                const std::uint32_t v = px[std::size_t{y} * w + x];
                const bool opaque = format == SlotFormat::Bgra ? (v >> 24) != 0 : format == SlotFormat::Bgrx ? (v & 0xFFFFFF) != 0 : (v & 0xFF) != 0;
                if (opaque) sp->hit_bits[std::size_t{y} * pitch + (x >> 3)] |= static_cast<std::uint8_t>(1u << (x & 7));
            }
        return 0;
    });
    // 90:3D (object) -> hit: whether the pointer is over the object, through its hit mask (dispobj.c: DispObj_HitTest)
    add(r, 0x903D, "object hit test", 1, 1, [s, display](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok() || !display) return 0;
        Sprite* sp = display->find_sprite(h);
        if (!sp) { c.fail(Errc::Range, "no display object " + std::to_string(h)); return 0; }
        std::int32_t l = 0, t = 0, rr = 0, b = 0;
        const SlotInfo* own = display->slot(sp->slot);
        int hit = 0;
        if (own && display->screen_rect(h, l, t, rr, b)) {
            const std::int64_t x = s->mouse_x - l, y = s->mouse_y - t;
            if (x >= 0 && y >= 0 && x < own->width && y < own->height) {
                if (!sp->has_hit_mask) hit = 1;
                else if (static_cast<std::uint32_t>(x) < sp->hit_w && static_cast<std::uint32_t>(y) < sp->hit_h)
                    hit = (sp->hit_bits[std::size_t(y) * ((sp->hit_w + 7) >> 3) + (std::size_t(x) >> 3)] >> (x & 7)) & 1;
            }
        }
        c.push(static_cast<Word>(hit));
        return 0;
    });
    // 90:3F (object): asks the object for its picture cache; no class of the reference overrides the default, so it is always an error
    add(r, 0x903F, "build object cache", 1, 0, [display](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok() || !display) return 0;
        if (!display->find_sprite(h)) { c.fail(Errc::Range, "no display object " + std::to_string(h)); return 0; }
        c.fail(Errc::Range, "the object cannot build a picture cache");
        return 0;
    });
    for (const Rec& rec : loose) {
        const std::uint16_t id = rec.id;
        const int pops = rec.pops, pushes = rec.pushes;
        add(r, id, rec.name, pops, pushes, [s, id, pops, pushes](Ctx& c) {
            std::vector<Word> a(static_cast<std::size_t>(pops));
            for (int i = pops - 1; i >= 0; --i) a[std::size_t(i)] = c.pop();
            if (!c.ok()) return 0;
            debug_object_op(id, a);
            s->records[id] = std::move(a);
            for (int i = 0; i < pushes; ++i) c.push(0);
            return 0;
        });
    }
    // sprite objects: ids 0x80000000 + slot (0..511), the lowest free slot first
    add(r, 0x9050, "create sprite object", 0, 1, [display](Ctx& c) {
        if (!display) return 0;
        for (Word slot = 0; slot < 512; ++slot) {
            if (display->find_sprite(0x80000000u + slot)) continue;
            Sprite& sp = display->sprite(0x80000000u + slot);
            sp = Sprite{};
            sp.visible = false;
            c.push(0x80000000u + slot);
            return 0;
        }
        c.fail(Errc::Range, "too many sprite objects (the limit is 512)");
        return 0;
    });
    add(r, 0x9051, "remove sprite object", 1, 0, [display](Ctx& c) {
        const Word id = c.pop();
        if (!c.ok() || !display) return 0;
        if (!display->find_sprite(id) || (id & 0xFFFFFE00u) != 0x80000000u) {
            c.fail(Errc::Range, "no sprite object " + std::to_string(id));
            return 0;
        }
        display->remove_sprite(id);
        return 0;
    });
    for (const Rec& rec : recs) {
        const std::uint16_t id = rec.id;
        const int pops = rec.pops, pushes = rec.pushes;
        add(r, id, rec.name, pops, pushes, [s, id, pops, pushes](Ctx& c) {
            std::vector<Word> a(static_cast<std::size_t>(pops));
            for (int i = pops - 1; i >= 0; --i) a[std::size_t(i)] = c.pop();
            if (!c.ok()) return 0;
            debug_object_op(id, a);
            s->records[id] = std::move(a);
            for (int i = 0; i < pushes; ++i) c.push(0);
            return 0;
        });
    }
    // (master, slave, dx, dy): the slave follows the master at (dx, dy); both objects must exist and differ
    add(r, 0x913E, "bind slave object", 4, 0, [display](Ctx& c) {
        const auto dy = static_cast<std::int32_t>(c.pop()), dx = static_cast<std::int32_t>(c.pop());
        const Word slave = c.pop(), master = c.pop();
        if (std::getenv("OBGI_DBGOBJ")) std::fprintf(stderr, "91:3E master %X slave %X at %d,%d\n", master, slave, dx, dy);
        if (!c.ok() || !display) return 0;
        Sprite* sl = display->find_sprite(slave);
        if (!sl) {
            c.fail(Errc::Range, "no display object " + std::to_string(slave));
            return 0;
        }
        if (!display->find_sprite(master) || master == slave) {
            c.fail(Errc::Range, "bad master object " + std::to_string(master) + " for slave " + std::to_string(slave));
            return 0;
        }
        sl->has_master = true, sl->master = master, sl->master_dx = dx, sl->master_dy = dy;
        const Sprite* ms = display->find_sprite(master);
        sl->x = ms->x + dx, sl->y = ms->y + dy; // moved there at once
        sl->moved = false;
        return 0;
    });
    // Groups (90:E0..E9): up to 8 invisible parent objects, handle 0xF1000000 | index. Members follow the group at their offsets
    // (the master/slave mechanism), and show / hide / level of the group are applied to them. Group sprites have no image slot.
    if (display) {
    constexpr Word group_tag = 0xF1000000u, no_image = 0xFFFFFFFFu;
    auto group_of = [display](Ctx& c, Word h) -> Sprite* {
        Sprite* g = (h & 0xFFFFFFF8u) == group_tag ? display->find_sprite(h) : nullptr;
        if (!g || g->slot != no_image) c.fail(Errc::Range, "no group " + std::to_string(h));
        return g && g->slot == no_image ? g : nullptr;
    };
    auto members = [display](Word group) {
        std::vector<Word> out;
        for (auto& [id, sp] : display->sprites())
            if (sp.has_master && sp.master == group) out.push_back(id);
        return out;
    };
    add(r, 0x90E0, "create group", 0, 1, [display](Ctx& c) {
        for (Word i = 0; i < 8; ++i)
            if (!display->find_sprite(group_tag | i)) {
                Sprite& g = display->sprite(group_tag | i);
                g = Sprite{};
                g.slot = no_image, g.visible = false;
                c.push(group_tag | i);
                return 0;
            }
        c.fail(Errc::Range, "no more groups (the limit is 8)");
        return 0;
    });
    add(r, 0x90E1, "delete group", 1, 0, [display, group_of, members](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok() || !group_of(c, h)) return 0;
        for (Word m : members(h)) display->find_sprite(m)->has_master = false;
        display->remove_sprite(h);
        return 0;
    });
    add(r, 0x90E4, "show group", 2, 0, [display, group_of](Ctx& c) {
        const Word on = c.pop(), h = c.pop();
        if (!c.ok() || !group_of(c, h)) return 0;
        display->set_visible(h, on != 0);
        return 0;
    });
    add(r, 0x90E5, "set group position and level", 4, 0, [display, group_of, members](Ctx& c) {
        const Word level = c.pop();
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Sprite* g = group_of(c, h);
        if (!g) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        g->x = x, g->y = y;
        display->set_level(h, level);
        return 0;
    });
    add(r, 0x90E8, "add object to group", 4, 0, [display, group_of](Ctx& c) {
        const auto dy = static_cast<std::int32_t>(c.pop()), dx = static_cast<std::int32_t>(c.pop());
        const Word obj = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        Sprite* g = group_of(c, h);
        if (!g) return 0;
        Sprite* o = display->find_sprite(obj);
        if (!o) { c.fail(Errc::Range, "no display object " + std::to_string(obj)); return 0; }
        if (obj == h) { c.fail(Errc::Range, "an object cannot be added to itself"); return 0; }
        if (std::getenv("OBGI_DBGOBJ")) std::fprintf(stderr, "group %X add %X at %d,%d (visible %d alpha %u slot %u)\n", h, obj, dx, dy, o->visible, o->alpha, o->slot);
        if (o->has_master) { c.fail(Errc::Range, "the object already has an owner"); return 0; }
        o->has_master = true, o->master = h, o->master_dx = dx, o->master_dy = dy, o->moved = false;
        o->x = g->x + dx, o->y = g->y + dy;
        return 0;
    });
    add(r, 0x90E9, "remove object from group", 2, 0, [display, group_of](Ctx& c) {
        const Word obj = c.pop(), h = c.pop();
        if (!c.ok() || !group_of(c, h)) return 0;
        Sprite* o = display->find_sprite(obj);
        if (!o) { c.fail(Errc::Range, "no display object " + std::to_string(obj)); return 0; }
        if (!o->has_master || o->master != h) { c.fail(Errc::Range, "the object is not in the group"); return 0; }
        o->has_master = false;
        return 0;
    });
    }
    // Child windows (B0:10..1C): pop-up OS windows of the original. There is a single screen here, so only their bookkeeping is kept
    // (up to 8, handles 1..8, size 0x20..0x400, position); nothing is drawn into them.
    struct Child { bool used = false; std::int32_t x = 0, y = 0; };
    auto children = std::make_shared<std::array<Child, 8>>();
    auto child_of = [children](Ctx& c, Word h) -> Child* {
        const bool ok = h >= 1 && h <= 8 && (*children)[h - 1].used;
        if (!ok) c.fail(Errc::Range, "no child window " + std::to_string(h));
        return ok ? &(*children)[h - 1] : nullptr;
    };
    add(r, 0xB010, "create child window", 5, 1, [children](Ctx& c) {
        const auto h = static_cast<std::int32_t>(c.pop()), w = static_cast<std::int32_t>(c.pop());
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        c.pop(); // title
        if (!c.ok()) return 0;
        if (w < 0x20 || w > 0x400 || h < 0x20 || h > 0x400) { c.fail(Errc::Range, "bad child window size"); return 0; }
        for (Word i = 0; i < 8; ++i)
            if (!(*children)[i].used) { (*children)[i] = {true, x, y}; c.push(i + 1); return 0; }
        c.fail(Errc::Range, "no more child windows");
        return 0;
    });
    add(r, 0xB011, "destroy child window", 1, 0, [child_of](Ctx& c) { const Word h = c.pop(); if (c.ok()) if (Child* ch = child_of(c, h)) ch->used = false; return 0; });
    add(r, 0xB014, "show child window", 2, 0, [child_of](Ctx& c) { c.pop(); const Word h = c.pop(); if (c.ok()) child_of(c, h); return 0; });
    add(r, 0xB015, "child window title", 2, 0, [child_of](Ctx& c) { c.pop(); const Word h = c.pop(); if (c.ok()) child_of(c, h); return 0; });
    add(r, 0xB016, "move child window", 3, 0, [child_of](Ctx& c) {
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (c.ok()) if (Child* ch = child_of(c, h)) ch->x = x, ch->y = y;
        return 0;
    });
    add(r, 0xB017, "child window position", 1, 2, [child_of](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Child* ch = child_of(c, h);
        c.push(ch ? Word(ch->x) : 0), c.push(ch ? Word(ch->y) : 0);
        return 0;
    });
    add(r, 0xB018, "fill child window", 2, 0, [child_of](Ctx& c) { c.pop(); const Word h = c.pop(); if (c.ok()) child_of(c, h); return 0; });
    add(r, 0xB019, "draw bitmap in child window", 6, 0, [child_of](Ctx& c) {
        for (int i = 0; i < 5; ++i) c.pop();
        const Word h = c.pop();
        if (c.ok()) child_of(c, h);
        return 0;
    });
    add(r, 0xB01A, "draw text in child window", 10, 1, [child_of](Ctx& c) {
        for (int i = 0; i < 9; ++i) c.pop();
        const Word h = c.pop();
        if (c.ok()) child_of(c, h);
        c.push(0); // the width of the widest line: not measured, nothing is drawn
        return 0;
    });
    add(r, 0xB01C, "child window copy text", 2, 0, [child_of](Ctx& c) { const Word h = c.pop(); c.pop(); if (c.ok()) child_of(c, h); return 0; });
    // Effectors (91:60..68, effector.c): up to 8 screen effect objects, handle 0x91000000 | index. Each is a display object that
    // Display::compose() draws in its sort position: it snapshots everything drawn below it and writes back a displaced (mode 0, 91:65),
    // blurred (1, 91:66), rippled (2, 91:67) or rotated and zoomed (3, 91:68) copy. The level is the strength.
    auto effectors = std::make_shared<std::array<bool, 8>>();
    auto effector_of = [effectors, display](Ctx& c, Word h) -> Sprite* {
        const bool ok = display && (h & 0xFFFFFFF8u) == 0x91000000u && (*effectors)[h & 7];
        if (!ok) {
            c.fail(Errc::Range, "no effector " + std::to_string(h));
            return nullptr;
        }
        return display->find_sprite(h);
    };
    add(r, 0x9160, "create effector", 0, 1, [effectors, display](Ctx& c) {
        if (!display) { c.push(0); return 0; }
        for (Word i = 0; i < 8; ++i)
            if (!(*effectors)[i]) {
                (*effectors)[i] = true;
                Sprite& sp = display->sprite(0x91000000u | i);
                sp = Sprite{};
                sp.effector = true, sp.visible = false, sp.priority = 0, sp.alpha = 0;
                c.push(0x91000000u | i);
                return 0;
            }
        c.fail(Errc::Range, "no more effectors (the limit is 8)");
        return 0;
    });
    add(r, 0x9161, "delete effector", 1, 0, [effectors, effector_of, display](Ctx& c) {
        const Word h = c.pop();
        if (c.ok() && effector_of(c, h)) (*effectors)[h & 7] = false, display->remove_sprite(h);
        return 0;
    });
    add(r, 0x9164, "show effector", 2, 0, [effector_of, display](Ctx& c) {
        const Word on = c.pop(), h = c.pop();
        if (c.ok() && effector_of(c, h)) display->set_visible(h, on != 0);
        return 0;
    });
    // 91:65 (h, vec1, vec2, level, amount, priority): mode 0, the screen below displaced through one screen-sized vector map, or two
    // (vec2 -1 = none) mixed by the level; a non-zero amount samples bilinearly
    add(r, 0x9165, "set effector displacement", 6, 0, [effector_of, display](Ctx& c) {
        const Word prio = c.pop();
        const auto amount = static_cast<std::int32_t>(c.pop());
        const Word level = c.pop();
        const auto vec2 = static_cast<std::int32_t>(c.pop()), vec1 = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (prio > 0xffff) { c.fail(Errc::Range, "priority out of range"); return 0; }
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        Sprite* sp = effector_of(c, h);
        if (!sp) return 0;
        const auto screen_vec = [display](std::int32_t id) {
            const auto it = display->vecmaps.find(static_cast<std::uint32_t>(id));
            return it != display->vecmaps.end() && it->second.mode == vecmap::Mode::Vector && it->second.w == display->screen_width() && it->second.h == display->screen_height();
        };
        if (!screen_vec(vec1)) { c.fail(Errc::Range, "vector map " + std::to_string(vec1) + " is missing or not screen sized"); return 0; }
        if (vec2 != -1 && !screen_vec(vec2)) { c.fail(Errc::Range, "vector map " + std::to_string(vec2) + " is missing or not screen sized"); return 0; }
        sp->fx.mode = 0, sp->fx.vec1 = vec1, sp->fx.vec2 = vec2, sp->fx.amount = amount;
        sp->alpha = level, sp->priority = prio;
        return 0;
    });
    // 91:66 (h, type, level, priority): mode 1, a horizontal box blur (type 0 pads with black, 1 repeats the edge)
    add(r, 0x9166, "set effector gradient", 4, 0, [effector_of](Ctx& c) {
        const Word prio = c.pop(), level = c.pop(), type = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (prio > 0xffff) { c.fail(Errc::Range, "priority out of range"); return 0; }
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        Sprite* sp = effector_of(c, h);
        if (!sp) return 0;
        if (type > 1) { c.fail(Errc::Range, "invalid gradient type " + std::to_string(type)); return 0; }
        sp->fx.mode = 1, sp->fx.grad_type = static_cast<std::int32_t>(type);
        sp->alpha = level, sp->priority = prio;
        return 0;
    });
    // 91:67 (h, map, rings, ripple, level, priority): mode 2, the screen below read through a screen-sized vector + distance map and the
    // ring table of ripple definition `ripple` (92:00); the level is the amplitude
    add(r, 0x9167, "set effector ripple", 6, 0, [effector_of, display](Ctx& c) {
        const Word prio = c.pop(), level = c.pop();
        const auto ripple = static_cast<std::int32_t>(c.pop()), rings = static_cast<std::int32_t>(c.pop()), map = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (prio > 0xffff) { c.fail(Errc::Range, "priority out of range"); return 0; }
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        Sprite* sp = effector_of(c, h);
        if (!sp) return 0;
        const auto it = display->vecmaps.find(static_cast<std::uint32_t>(map));
        if (it == display->vecmaps.end() || it->second.mode != vecmap::Mode::VecDist || it->second.w != display->screen_width() || it->second.h != display->screen_height()) {
            c.fail(Errc::Range, "vector + distance map " + std::to_string(map) + " is missing or not screen sized");
            return 0;
        }
        if (rings == 0) { c.fail(Errc::Range, "invalid maximum distance 0"); return 0; }
        bool ok = false;
        if (vecmap::ripple_check(display->ripples, ok, ripple, 0, rings) != 0) { c.fail(Errc::Range, "ripple " + std::to_string(ripple) + " is not registered"); return 0; }
        if (!ok) { c.fail(Errc::Range, "ripple " + std::to_string(ripple) + " does not fit the map " + std::to_string(map)); return 0; }
        sp->fx.mode = 2, sp->fx.map = map, sp->fx.rings = rings, sp->fx.ripple_no = ripple, sp->fx.ripple_sel = 0;
        sp->alpha = level, sp->priority = prio;
        return 0;
    });
    // 91:68 (h, cx, cy, angle, sx, sy, smooth, level, priority): mode 3, rotation by `angle` and zoom by (sx, sy) (16.16) about the screen
    // point (cx, cy); the level blends from the identity, the progress (90:35) and 90:38's 0x80-0x82/0x8F animate it. A zero scale is the
    // only error; an unknown handle is ignored, as in the original
    add(r, 0x9168, "set effector zoom", 9, 0, [effectors, display](Ctx& c) {
        const Word prio = c.pop(), level = c.pop(), smooth = c.pop();
        const auto sy = static_cast<std::int32_t>(c.pop()), sx = static_cast<std::int32_t>(c.pop()), angle = static_cast<std::int32_t>(c.pop());
        const auto cy = static_cast<std::int32_t>(c.pop()), cx = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (prio > 0xffff) { c.fail(Errc::Range, "priority out of range"); return 0; }
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        if (sx == 0 || sy == 0) { c.fail(Errc::Range, "invalid scale " + std::to_string(sx) + "," + std::to_string(sy)); return 0; }
        Sprite* sp = display && (h & 0xFFFFFFF8u) == 0x91000000u && (*effectors)[h & 7] ? display->find_sprite(h) : nullptr;
        if (!sp) return 0;
        sp->fx.mode = 3, sp->fx.cx = cx, sp->fx.cy = cy, sp->fx.angle = angle, sp->fx.sx = sx, sp->fx.sy = sy, sp->fx.smooth = static_cast<std::int32_t>(smooth);
        sp->fx.d_cx = sp->fx.d_cy = sp->fx.d_angle = sp->fx.d_sx = sp->fx.d_sy = sp->fx.curve = 0;
        sp->alpha = level, sp->priority = prio;
        display->set_progress(h, 0);
        return 0;
    });
    // 91:DB: the knob being dragged, 0 when none
    add(r, 0x91DB, "dragged knob", 0, 1, [s](Ctx& c) { c.push(s->knob_focus); return 0; });
    // Video textures (91:F0..F7): not modelled; they answer as the reference engine does without a player: 1 when the bitmap slot
    // exists, 4 when it does not.
    if (display) {
        add(r, 0x91F0, "open video texture", 4, 1, [](Ctx& c) { c.pop(), c.pop(), c.pop(), c.pop(); c.push(1); return 0; });
        for (auto [id, n] : {std::pair{0x91F1, 2}, {0x91F2, 1}, {0x91F3, 2}, {0x91F4, 5}, {0x91F5, 1}, {0x91F6, 1}})
            add(r, static_cast<std::uint16_t>(id), "video texture (not modelled)", n, 1, [display, n, id](Ctx& c) {
                std::vector<Word> a(static_cast<std::size_t>(n));
                for (int i = n - 1; i >= 0; --i) a[std::size_t(i)] = c.pop();
                if (!c.ok()) return 0;
                const Word bmp = a[0];
                c.push(display->slot(bmp) ? 1 : 4);
                return 0;
            });
        add(r, 0x91F7, "video texture frame", 2, 1, [display](Ctx& c) {
            const Word out = c.pop(), bmp = c.pop();
            if (!c.ok()) return 0;
            if (!display->slot(bmp)) { c.push(0); return 0; }
            c.write(out, 0, 4);
            c.push(1);
            return 0;
        });
    }
    // (slave, ?): detaches a slave object
    add(r, 0x913F, "unbind slave object", 2, 0, [display](Ctx& c) {
        const Word unused = c.pop(), slave = c.pop();
        (void)unused;
        if (!c.ok() || !display) return 0;
        Sprite* sl = display->find_sprite(slave);
        if (!sl) {
            c.fail(Errc::Range, "no display object " + std::to_string(slave));
            return 0;
        }
        sl->has_master = false;
        return 0;
    });
}

// Text settings of a plane: `n` numbers, the first is the plane handle; the rest are kept under the op's id until the
// text drawing that reads them exists. The original's range checks on the individual numbers are not reproduced.
void add_plane_setting(vm::SubopRegistry& r, SystemState& s, std::uint16_t id, const char* name, int n) {
    SystemState* st = &s;
    add(r, id, name, n, 0, [st, id, n](Ctx& c) {
        std::vector<Word> a(static_cast<std::size_t>(n));
        for (int i = n - 1; i >= 0; --i) a[std::size_t(i)] = c.pop();
        if (!c.ok()) return 0;
        debug_object_op(id, a);
        if (SystemState::Plane* p = plane_of(*st, c, a[0])) p->props[id].assign(a.begin() + 1, a.end());
        return 0;
    });
}

// The original's Ease(): progress t (0..0x1000000 = 1.0) to a 16.16 factor. Curve 0 is linear, 1..3 sine easing, 4..15 powers.
std::int32_t ease(std::int32_t t, int curve) { return bgfx::ease(t, curve); }

// The natural cubic spline through `pts` (at least three, knots one apart) at u in 0..1, per axis. Second derivatives m solve
// m[i-1] + 4 m[i] + m[i+1] = 6 (y[i-1] - 2 y[i] + y[i+1]) with m = 0 at both ends.
std::array<double, 3> spline_at(const std::vector<std::array<double, 3>>& pts, double u) {
    const int n = static_cast<int>(pts.size()) - 1;
    const double x = std::clamp(u, 0.0, 1.0) * n;
    const int i = std::min(static_cast<int>(x), n - 1);
    const double t = x - i;
    std::array<double, 3> out{};
    for (int axis = 0; axis < 3; ++axis) {
        std::vector<double> m(static_cast<std::size_t>(n) + 1, 0.0), diag(static_cast<std::size_t>(n) + 1, 4.0), rhs(static_cast<std::size_t>(n) + 1, 0.0);
        for (int k = 1; k < n; ++k) rhs[k] = 6 * (pts[k - 1][axis] - 2 * pts[k][axis] + pts[k + 1][axis]);
        for (int k = 2; k < n; ++k) { // forward elimination of the sub-diagonal
            const double f = 1.0 / diag[k - 1];
            diag[k] -= f, rhs[k] -= f * rhs[k - 1];
        }
        for (int k = n - 1; k >= 1; --k) m[k] = (rhs[k] - (k + 1 < n ? m[k + 1] : 0.0)) / diag[k];
        const double a = pts[i][axis], b = pts[i + 1][axis];
        out[axis] = (1 - t) * a + t * b + (((1 - t) * (1 - t) * (1 - t) - (1 - t)) * m[i] + (t * t * t - t) * m[i + 1]) / 6.0;
    }
    return out;
}

// Starts a timed change of display object `id`: to (x, y) along `curve` and to alpha `level` along `level_curve`, over
// `duration` ms. The object becomes visible; scripts wait for the end with 80:5C. `move` = false leaves the position alone.
// With a `path` the position follows the object's start followed by those points (90:29).
std::int64_t start_motion(SystemState& s, Display& display, Ctx& c, Word id, bool move, std::int32_t x, std::int32_t y, int curve, Word level,
                  int level_curve, Word duration, std::int32_t progress = -1, std::vector<std::array<double, 3>> path = {}) {
    Sprite* sp = display.find_sprite(id);
    if (!sp && id >= 0x90000000u) return 0; // filters, effectors, windows and the like are not drawn objects here
    if (!sp) { c.fail(Errc::Range, "no display object " + std::to_string(id)); return 0; }
    SystemState::Motion m;
    m.object = id;
    m.start_ms = c.vm.now_ms;
    m.duration_ms = duration;
    m.from_x = sp->script_x(), m.from_y = sp->script_y();
    m.to_x = move ? x : m.from_x, m.to_y = move ? y : m.from_y;
    m.curve = curve;
    m.from_level = static_cast<std::int32_t>(sp->alpha), m.to_level = static_cast<std::int32_t>(level);
    m.level_curve = level_curve;
    m.from_progress = static_cast<std::int32_t>(sp->progress);
    m.to_progress = progress < 0 ? m.from_progress : progress; // -1 keeps it
    m.from_z = sp->depth, m.to_z = sp->depth;
    if (!path.empty()) { // the start plus the points: two points are a straight line, more make a spline
        m.path.push_back({sp->script_fixed_x() / 65536.0, sp->script_fixed_y() / 65536.0, m.from_z / 65536.0});
        m.path.insert(m.path.end(), path.begin(), path.end());
        m.to_z = static_cast<std::int32_t>(path.back()[2] * 65536.0);
    }
    m.from_level = static_cast<std::int32_t>(sp->level());
    display.set_visible(id, true);
    if (std::getenv("OBGI_DBGOBJ"))
        std::fprintf(stderr, "motion object %X: %d,%d -> %d,%d level %d -> %d over %u ms\n", id, m.from_x, m.from_y, m.to_x, m.to_y, m.from_level, m.to_level, duration);
    std::erase_if(s.motions, [&](const auto& o) { return o.object == id; });
    if (m.duration_ms <= 0) {
        if (move) {
            if (!m.path.empty()) sp->script_fixed_move_to(static_cast<std::int32_t>(m.path.back()[0] * 65536.0), static_cast<std::int32_t>(m.path.back()[1] * 65536.0));
            else sp->script_move_to(m.to_x, m.to_y);
            sp->depth = m.to_z;
        }
        display.set_level(id, level);
        if (m.to_progress != m.from_progress) display.set_progress(id, static_cast<std::uint32_t>(m.to_progress));
        return 1;
    }
    s.motions.push_back(m);
    c.t.objects_until = std::max(c.t.objects_until, m.start_ms + m.duration_ms);
    return m.duration_ms;
}

void wait_for_motion(Ctx& c, std::int64_t duration_ms) {
    const auto duration = std::max<std::int64_t>(duration_ms, 1);
    const auto updates = (duration + 15) / 16;
    vm::WaitObject wait;
    wait.kind = vm::WaitObject::Kind::Time;
    wait.until = c.vm.now_ms + duration;
    wait.values = {static_cast<Word>(updates * 1000 / duration), 0};
    c.suspend(std::move(wait), 2);
}

// Round half away from zero (wait_tween.c: RoundToInt); a non-finite value (a via point with the same x as the start) gives 0.
std::int32_t round_away(double v) {
    if (!std::isfinite(v)) return 0;
    return static_cast<std::int32_t>(v < 0.0 ? v - 0.5 : v + 0.5);
}

// The natural cubic spline through the three knots (X[i], Y[i]) at t, in the original's operation order (wait_tween.c: Spline3).
double spline3(const double* X, const double* Y, double t) {
    const int seg = t < X[1] ? 0 : 1;
    const double h[2] = {X[1] - X[0], X[2] - X[1]};
    const double F[3] = {0.0, ((Y[2] - Y[1]) / h[1] - (Y[1] - Y[0]) / h[0]) * 3.0 / ((h[0] + h[1]) * 2.0), 0.0};
    const double dx = t - X[seg];
    const double b = (Y[seg + 1] - Y[seg]) / h[seg] - (F[seg] + F[seg] + F[seg + 1]) * h[seg] * 0.3333333333333333;
    const double d = (F[seg + 1] - F[seg]) / (h[seg] * 3.0);
    return ((d * dx + F[seg]) * dx + b) * dx + Y[seg];
}

// `count` points of the curve from (x0, y0) through (x1, y1) to (x2, y2): y a spline of x, equally spaced in x. False when the
// x values are not monotonic. A decreasing run is mirrored so the spline sees increasing x (wait_tween.c: Curve_Build).
bool build_via_curve(std::vector<std::array<std::int32_t, 2>>& out, std::int32_t x0, std::int32_t y0, std::int32_t x1, std::int32_t y1,
                     std::int32_t x2, std::int32_t y2, std::uint32_t count) {
    if (x0 < x1 ? x1 > x2 : (x0 > x1 && x1 < x2)) return false;
    bool increasing = true;
    if (!(x0 < x1 && x1 < x2)) {
        x0 = -x0, x1 = -x1, x2 = -x2;
        increasing = false;
    }
    const double X[3] = {0.0, double(x1 - x0), double(x2 - x0)}, Y[3] = {0.0, double(y1 - y0), double(y2 - y0)};
    double cur = X[0];
    const double step = (X[2] - X[0]) / double(count - 1);
    out.resize(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::int32_t xi = round_away(cur);
        out[i] = {increasing ? xi + x0 : -(xi + x0), round_away(spline3(X, Y, cur)) + y0};
        cur += step;
    }
    return true;
}

// 90:24 (wait_tween.c: WaitMove2_Start): frames = duration * fps / 1000 (at least 1), one point of the curve per frame plus one,
// the alpha going linearly to `level`. A script error for a rate below 1 and for x values that are not monotonic.
std::int64_t start_via_motion(SystemState& s, Display& display, Ctx& c, Word id, std::int32_t via_x, std::int32_t via_y, std::int32_t x, std::int32_t y, int curve,
                      Word level, Word duration, Word fps) {
    if (fps < 1) { c.fail(Errc::Range, "invalid frame rate"); return 0; }
    Sprite* sp = display.find_sprite(id);
    if (!sp && id >= 0x90000000u) return 0;
    if (!sp) { c.fail(Errc::Range, "no display object " + std::to_string(id)); return 0; }
    SystemState::Motion m;
    m.kind = SystemState::Motion::Kind::Via;
    m.object = id, m.start_ms = c.vm.now_ms;
    m.frames = std::max<std::int32_t>(1, static_cast<std::int32_t>(static_cast<std::uint32_t>(duration * fps) / 1000u));
    m.frame_ms = m.frames > 1 ? static_cast<std::int32_t>(1000u / fps) : static_cast<std::int32_t>(duration); // one frame takes the whole duration
    m.duration_ms = std::int64_t{m.frames} * m.frame_ms;
    m.from_x = sp->script_x(), m.from_y = sp->script_y(), m.to_x = x, m.to_y = y;
    if (!build_via_curve(m.via_points, m.from_x, m.from_y, via_x, via_y, x, y, static_cast<std::uint32_t>(m.frames) + 1)) {
        c.fail(Errc::Range, "90:24: the x values of the start, via point and end must advance in one direction");
        return 0;
    }
    m.curve = curve;
    m.from_level = static_cast<std::int32_t>(sp->alpha), m.to_level = static_cast<std::int32_t>(level);
    display.set_visible(id, true);
    std::erase_if(s.motions, [&](const auto& o) { return o.object == id; });
    if (m.duration_ms <= 0) {
        sp->script_move_to(x, y), display.set_level(id, level);
        return 1;
    }
    s.motions.push_back(m);
    c.t.objects_until = std::max(c.t.objects_until, m.start_ms + m.duration_ms);
    return m.duration_ms;
}

// 90:2C (wait_tween.c: WaitObjQuake_Start): shakes the object about where it is. Pattern 0..5 (0, 1, 4 vertical, 2, 3, 5 horizontal;
// 0 and 2 start towards the negative side, 4 and 5 half a cycle in); the amplitude loses `decay` percent per cycle. An object that
// does not exist is not reported.
std::int64_t start_quake(SystemState& s, Display& display, Ctx& c, Word id, Word pattern, std::int32_t amplitude, Word frequency, Word repeat, std::int32_t decay, Word fps) {
    Sprite* sp = display.find_sprite(id);
    if (!sp && id < 0x90000000u) return 0;
    if (pattern >= 6) { c.fail(Errc::Range, "invalid shake pattern " + std::to_string(pattern)); return 0; }
    if (frequency < 1) { c.fail(Errc::Range, "invalid shake frequency " + std::to_string(frequency)); return 0; }
    if (repeat < 1) { c.fail(Errc::Range, "invalid shake repeat count " + std::to_string(repeat)); return 0; }
    if (fps < 1 || fps < frequency) { c.fail(Errc::Range, "invalid frame rate " + std::to_string(fps)); return 0; }
    if (!sp) return 0;
    SystemState::Motion m;
    m.kind = SystemState::Motion::Kind::Quake;
    m.object = id, m.start_ms = c.vm.now_ms;
    m.frame_ms = static_cast<std::int32_t>(1000u / fps);
    m.cycle_frames = std::max<std::int32_t>(1, static_cast<std::int32_t>(1000u / std::uint32_t(std::max<std::int32_t>(1, m.frame_ms * std::int32_t(frequency)))));
    m.frames = m.cycle_frames * static_cast<std::int32_t>(repeat);
    m.duration_ms = std::int64_t{m.frames} * m.frame_ms;
    m.quake_pattern = static_cast<std::int32_t>(pattern);
    m.from_x = sp->script_x(), m.from_y = sp->script_y();
    std::int64_t amp = static_cast<std::int64_t>(static_cast<std::uint32_t>(amplitude)) << 16; // zero-extended, as the original's pair
    for (Word i = 0; i < repeat && i < 0x10000; ++i) {
        m.quake_amps.push_back(static_cast<std::int32_t>(amp >> 16));
        amp = amp * (100 - decay) / 100;
    }
    std::int32_t step = static_cast<std::int32_t>(0x20000u / static_cast<std::uint32_t>(m.cycle_frames));
    const std::int32_t phase0 = pattern < 4 ? 0 : 0x8000;
    std::int32_t phase = phase0;
    for (std::int32_t i = 0; i < m.cycle_frames; ++i) { // one cycle of a triangle wave
        phase += step;
        if (phase >= 0x10000) step = -step, phase = 0x20000 - phase;
        if (phase <= 0) step = -step, phase = -phase;
        m.quake_wave.push_back(pattern == 0 || pattern == 2 ? phase0 - phase : phase - phase0);
    }
    display.set_visible(id, true);
    std::erase_if(s.motions, [&](const auto& o) { return o.object == id; });
    s.motions.push_back(m);
    c.t.objects_until = std::max(c.t.objects_until, m.start_ms + m.duration_ms);
    return m.duration_ms;
}

// One step of the frame-stepped motions: the frame is the time elapsed over the frame interval, at most the last one.
void step_framed(const SystemState::Motion& m, Sprite& sp, Display& display, std::int64_t elapsed) {
    const std::int32_t cur = m.frame_ms > 0 ? static_cast<std::int32_t>(std::min<std::int64_t>(elapsed / m.frame_ms, m.frames)) : m.frames;
    const bool done = cur >= m.frames;
    if (m.kind == SystemState::Motion::Kind::Via) {
        std::int32_t x = m.to_x, y = m.to_y, level = m.to_level;
        if (!done) {
            const std::int32_t f = ease(static_cast<std::int32_t>(std::int64_t{cur} * 0x1000000 / m.frames), m.curve);
            const auto idx = std::min<std::int64_t>((std::int64_t{m.frames} + 1) * f >> 16, std::int64_t(m.via_points.size()) - 1);
            x = m.via_points[std::size_t(idx)][0], y = m.via_points[std::size_t(idx)][1];
            level = (m.to_level - m.from_level) * cur / m.frames + m.from_level;
        }
        sp.script_move_to(x, y);
        display.set_level(m.object, static_cast<std::uint32_t>(level));
        return;
    }
    std::int32_t x = m.from_x, y = m.from_y;
    if (!done) {
        const auto cycle = static_cast<std::size_t>(cur / m.cycle_frames), frame = static_cast<std::size_t>(cur % m.cycle_frames);
        const auto off = static_cast<std::int32_t>((std::int64_t{static_cast<std::uint32_t>(m.quake_amps[cycle])} * m.quake_wave[frame]) >> 16);
        (m.quake_pattern == 2 || m.quake_pattern == 3 || m.quake_pattern == 5 ? x : y) += off;
    }
    sp.script_move_to(x, y);
}

} // namespace

void advance_motions(SystemState& state, Display& display, std::int64_t now_ms) {
    for (auto it = state.motions.begin(); it != state.motions.end();) {
        Sprite* sp = display.find_sprite(it->object);
        const std::int64_t elapsed = std::min(now_ms - it->start_ms, it->duration_ms);
        if (sp && it->duration_ms > 0 && it->kind != SystemState::Motion::Kind::Tween) {
            step_framed(*it, *sp, display, elapsed);
        } else if (sp && it->duration_ms > 0) {
            const bool done = elapsed >= it->duration_ms;
            const auto t = static_cast<std::int32_t>(elapsed * 0x1000000 / it->duration_ms);
            const std::int64_t fp = done ? 0x10000 : ease(t, it->curve), fl = done ? 0x10000 : ease(t, it->level_curve);
            if (it->path.size() >= 3) {
                const auto p = spline_at(it->path, double(fp) / 65536.0);
                sp->script_fixed_move_to(static_cast<std::int32_t>(p[0] * 65536.0), static_cast<std::int32_t>(p[1] * 65536.0));
                sp->depth = static_cast<std::int32_t>(p[2] * 65536.0);
            } else if (it->path.size() == 2) { // interpolate in the original 16.16 units, retaining negative fractions
                const auto fixed_axis = [fp](double a, double b) {
                    const auto from = static_cast<std::int32_t>(a * 65536.0);
                    return from + static_cast<std::int32_t>((b - a) * fp);
                };
                sp->script_fixed_move_to(fixed_axis(it->path[0][0], it->path[1][0]), fixed_axis(it->path[0][1], it->path[1][1]));
                sp->depth = fixed_axis(it->path[0][2], it->path[1][2]);
            } else {
                sp->script_move_to(it->from_x + static_cast<std::int32_t>((std::int64_t{it->to_x} - it->from_x) * fp >> 16),
                            it->from_y + static_cast<std::int32_t>((std::int64_t{it->to_y} - it->from_y) * fp >> 16));
            }
            display.set_level(it->object, static_cast<std::uint32_t>(it->from_level + static_cast<std::int32_t>((std::int64_t{it->to_level} - it->from_level) * fl >> 16)));
            if (it->to_progress != it->from_progress) // linear in time, whatever the curves
                display.set_progress(it->object, static_cast<std::uint32_t>(it->from_progress + (std::int64_t{it->to_progress} - it->from_progress) * elapsed / it->duration_ms));
        }
        it = (!sp || now_ms - it->start_ms >= it->duration_ms) ? state.motions.erase(it) : it + 1;
    }
    for (auto& task : state.per_frame) task(now_ms);
    panel_sync(state);
    panel_input(state);
    knob_input(state);
}

void register_plane_subops(vm::SubopRegistry& r, SystemState& state, Display* display) {
    SystemState* s = &state;
    if (display) state.display = display;

    register_object_settings(r, state, display);

    add_plane_setting(r, state, 0x9188, "window object font", 7);
    add_plane_setting(r, state, 0x9189, "window object line gap", 2);
    add_plane_setting(r, state, 0x918A, "window object text direction", 2);
    add_plane_setting(r, state, 0x918B, "window object text sway", 2);
    // 90:28, popped in this order: priority, input-skip flag, total, rate, step, three more values (alpha-like, not
    // modelled), a value, y, x, object. The object moves to (x, y) linearly over total * 1000 / rate ms; 80:5C waits for it.
    // (analysis: FUN_0048a0e0 -> FUN_004a1070 -> CProcCtrlDspObj FUN_00437d60). A rate of 0 or a missing object is a script error.
    // Popped in this order: prio, skip, max skip, fps, duration (ms), progress, level curve, level, curve, y, x, object (the
    // reference engine's layout, docs/vm.md of the other project). A rate of 0 or a missing object is a script error.
    if (display) add(r, 0x9028, "move object over time", 12, 2, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        if (a[3] == 0) { c.fail(Errc::Range, "90:28: rate 0"); return 0; }
        const auto duration = start_motion(*s, *display, c, a[11], true, static_cast<std::int32_t>(a[10]), static_cast<std::int32_t>(a[9]), static_cast<int>(a[8]), a[7],
                                            static_cast<int>(a[6]), a[4], static_cast<std::int32_t>(a[5]));
        if (c.ok()) wait_for_motion(c, duration);
        return c.ok() ? vm::StayThread : 0;
    }, 2);
    // 90:20 fade: prio, skip, fps, duration, level, object. 90:22 adds max skip after skip.
    // 90:21 move: prio, skip, fps, duration, level, curve, y, x, object. 90:23 adds max skip after skip.
    if (display) {
        for (auto [id, n] : {std::pair{0x9020, 6}, {0x9022, 7}, {0x9021, 9}, {0x9023, 10}}) {
            const bool move = id == 0x9021 || id == 0x9023;
            const bool has_max = id == 0x9022 || id == 0x9023;
            add(r, static_cast<std::uint16_t>(id), move ? "move object (timed)" : "fade object (timed)", n, 2, [s, display, n, move, has_max](Ctx& c) {
                std::vector<Word> a(static_cast<std::size_t>(n));
                for (Word& w : a) w = c.pop();
                if (!c.ok()) return 0;
                const std::size_t i = has_max ? 3 : 2; // index of fps
                if (a[i] == 0) { c.fail(Errc::Range, "invalid frame rate"); return 0; }
                if (a[i + 2] > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
                const auto duration = move
                    ? start_motion(*s, *display, c, a[std::size_t(n) - 1], true, static_cast<std::int32_t>(a[std::size_t(n) - 2]),
                                   static_cast<std::int32_t>(a[std::size_t(n) - 3]), static_cast<int>(a[std::size_t(n) - 4]), a[i + 2], 0, a[i + 1])
                    : start_motion(*s, *display, c, a[std::size_t(n) - 1], false, 0, 0, 0, a[i + 2], 0, a[i + 1]);
                if (c.ok()) wait_for_motion(c, duration);
                return c.ok() ? vm::StayThread : 0;
            }, 2);
        }
    }
    // 90:29: the same over a path of n points (four 16.16 words each: x, y, z, padding); popped in this order: priority, skip,
    // max skip, fps, duration (ms), progress, level curve, level, curve, point address, n, object. The path is the object's
    // position followed by the points; one point is a line, more are a natural cubic spline, walked along the eased time.
    if (display) add(r, 0x9029, "move object along a path", 12, 2, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        const Word id = a[11], n = a[10], points = a[9], duration = a[4], fps = a[3];
        if (fps == 0 || n == 0) { c.fail(Errc::Range, "90:29: bad frame rate or point count"); return 0; }
        if (std::getenv("OBGI_DBGOBJ"))
            for (Word i = 0; i < n && i < 8; ++i)
                std::fprintf(stderr, "90:29 object %X point %u: raw %08X %08X %08X (curve %d, fps %u)\n", id, i, c.read(points + 16 * i, 4, false),
                             c.read(points + 16 * i + 4, 4, false), c.read(points + 16 * i + 8, 4, false), static_cast<int>(a[8]), fps);
        const Word last = points + 16 * (n - 1);
        const auto to_x = static_cast<std::int32_t>(c.read(last, 4, false)) >> 16;
        const auto to_y = static_cast<std::int32_t>(c.read(last + 4, 4, false)) >> 16;
        if (!c.ok()) return 0;
        std::vector<std::array<double, 3>> path;
        for (Word i = 0; i < n && i < 100; ++i) // the original holds at most 100 points
            path.push_back({static_cast<std::int32_t>(c.read(points + 16 * i, 4, false)) / 65536.0, static_cast<std::int32_t>(c.read(points + 16 * i + 4, 4, false)) / 65536.0,
                            static_cast<std::int32_t>(c.read(points + 16 * i + 8, 4, false)) / 65536.0});
        if (!c.ok()) return 0;
        const auto actual_duration = start_motion(*s, *display, c, id, true, to_x, to_y, static_cast<int>(a[8]), a[7], static_cast<int>(a[6]), duration,
                                                   static_cast<std::int32_t>(a[5]), std::move(path));
        if (c.ok()) wait_for_motion(c, actual_duration);
        return c.ok() ? vm::StayThread : 0;
    }, 2);
    // 90:F8-FD: click targets (sysobj/targets.c). F8 clears the list, FA registers a sprite (an error for anything else), FB removes it
    // (an error when it was not registered), FC answers the index of the newest target whose screen rectangle holds the pointer
    // (0xFFFFFFFF for none), FD the left-button state sampled on a target by index (an error for an unknown index).
    if (display) {
        const auto sprite_handle = [display](Word h) { return (h >> 28) == 8 && display->find_sprite(h) != nullptr; };
        const auto first_hit = [s, display]() -> std::uint32_t {
            for (const auto& t : s->targets) {
                std::int32_t l, tp, r, b;
                if (display->screen_rect(t.handle, l, tp, r, b) && s->mouse_x >= l && s->mouse_x <= r && s->mouse_y >= tp && s->mouse_y <= b) return t.index;
            }
            return 0xFFFFFFFFu;
        };
        add(r, 0x90F8, "clear click targets", 0, 0, [s](Ctx&) { s->targets.clear(), s->target_count = 0; return 0; });
        add(r, 0x90FA, "add click target", 1, 0, [s, sprite_handle](Ctx& c) {
            const Word h = c.pop();
            if (!c.ok()) return 0;
            if (!sprite_handle(h)) { c.fail(Errc::Range, "click target " + std::to_string(h) + " is not a sprite"); return 0; }
            s->targets.insert(s->targets.begin(), SystemState::ClickTarget{s->target_count++, h, false});
            return 0;
        });
        add(r, 0x90FB, "remove click target", 1, 0, [s](Ctx& c) {
            const Word h = c.pop();
            if (!c.ok()) return 0;
            const auto it = std::find_if(s->targets.begin(), s->targets.end(), [h](const auto& t) { return t.handle == h; });
            if (it == s->targets.end()) { c.fail(Errc::Range, "click target " + std::to_string(h) + " is not registered"); return 0; }
            s->targets.erase(it);
            return 0;
        });
        add(r, 0x90FC, "hit click target", 0, 1, [first_hit](Ctx& c) { c.push(first_hit()); return 0; });
        add(r, 0x90FD, "click target state", 1, 1, [s](Ctx& c) {
            const Word index = c.pop();
            if (!c.ok()) return 0;
            const auto it = std::find_if(s->targets.begin(), s->targets.end(), [index](const auto& t) { return t.index == index; });
            if (it == s->targets.end()) { c.fail(Errc::Range, "invalid click target number " + std::to_string(index)); return 0; }
            c.push(it->hit ? 1 : 0);
            return 0;
        });
        // Target_UpdateAll: the left button sampled over each target's layer, here the button held while the pointer is on the first target hit
        s->per_frame.push_back([s, first_hit](std::int64_t) {
            const std::uint32_t hit = s->left_down ? first_hit() : 0xFFFFFFFFu;
            for (auto& t : s->targets) t.hit = t.index == hit;
        });
    }
    // 90:24: through a via point on a cubic curve; popped: prio, skip, max skip, fps, duration, level, curve, y, x, via y, via x, object.
    if (display) add(r, 0x9024, "move object through a point", 12, 2, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        const auto duration = start_via_motion(*s, *display, c, a[11], static_cast<std::int32_t>(a[10]), static_cast<std::int32_t>(a[9]),
                                                static_cast<std::int32_t>(a[8]), static_cast<std::int32_t>(a[7]), static_cast<int>(a[6]), a[5], a[4], a[3]);
        if (c.ok()) wait_for_motion(c, duration);
        return c.ok() ? vm::StayThread : 0;
    }, 2);
    // 90:2C: shake an object; popped: prio, skip, fps, decay, repeat, frequency, amplitude, pattern, object.
    if (display) add(r, 0x902C, "shake object", 9, 2, [s, display](Ctx& c) {
        Word a[9];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        const auto duration = start_quake(*s, *display, c, a[8], a[7], static_cast<std::int32_t>(a[6]), a[5], a[4], static_cast<std::int32_t>(a[3]), a[2]);
        if (c.ok()) wait_for_motion(c, duration);
        return c.ok() ? vm::StayThread : 0;
    }, 2);
    // (handle): pushes one number (zero for now)
    add(r, 0x918E, "window object query 91:8E", 1, 1, [s](Ctx& c) {
        const Word handle = c.pop();
        if (!c.ok() || !plane_of(*s, c, handle)) return 0;
        c.push(1); // a fresh plane (cursor at the start of the text area): 1; it depends on the cursor and the layout area
        return 0;
    });

    // (width, height): creates a plane. Sizes below 0x20 / 0x14 are counted in units of 32 pixels. Width 1..0x2000,
    // height 1..0x8000 after that. At most 16 planes. Pushes the handle.
    add(r, 0x9080, "create window object", 2, 1, [s, display](Ctx& c) {
        Word h = c.pop(), w = c.pop();
        if (!c.ok()) return 0;
        const Word w_in = w;
        if (w < 0x20) w <<= 5;
        if (h < 0x14) h <<= 5;
        std::size_t slot = 0;
        while (slot < s->planes.size() && s->planes[slot]) ++slot;
        if (slot == s->planes.size()) {
            c.fail(Errc::Range, "too many window objects (the limit is 16)");
            return 0;
        }
        if (w == 0 || w > 0x2000 || h == 0 || h > 0x8000) {
            c.fail(Errc::Range, "window object size " + std::to_string(w_in) + " x " + std::to_string(h) + " is not allowed");
            return 0;
        }
        SystemState::Plane p;
        p.width = w, p.height = h;
        p.area = {0, 0, std::int32_t(w) - 1, std::int32_t(h) - 1};
        if (display) {
            const Word slot_id = kPlaneSlotBase + static_cast<Word>(slot);
            if (auto made = display->create_slot(slot_id, w, h, SlotFormat::Bgra); !made) {
                c.fail(made.error().code, made.error().message);
                return 0;
            }
            display->fill_slot(slot_id, 0);
            Sprite& sp = display->sprite(kHandleBase + static_cast<Word>(slot));
            sp = Sprite{};
            sp.slot = slot_id;
            sp.visible = false; // windows start hidden; 90:84 shows them
        }
        s->plane_shown[slot] = false, s->plane_level[slot] = 0, s->plane_effect[slot] = 1;
        s->planes[slot] = std::move(p);
        c.push(kHandleBase + static_cast<Word>(slot));
        return 0;
    });
    // (handle): removes a plane
    add(r, 0x9081, "remove window object", 1, 0, [s, display](Ctx& c) {
        const Word handle = c.pop();
        if (!c.ok() || !plane_of(*s, c, handle)) return 0;
        for (auto it = s->icon_controls.begin(); it != s->icon_controls.end();) {
            if (it->second.window == handle && it->second.paint_only) {
                panel_stop(*s, it->first);
                it = s->icon_controls.erase(it);
            } else ++it;
        }
        if (display) {
            display->remove_sprite(handle);
            display->free_slot(kPlaneSlotBase + (handle & 0xFFFFFFu));
            (void)display->free_slot(0x20000u + (handle & 0xFFFFFFu));
            (void)display->free_slot(0x30000u + (handle & 0xFFFFFFu));
        }
        s->planes[handle & 0xFFFFFFu].reset();
        return 0;
    });
    // (handle, layer order 0..5)
    add(r, 0x9082, "window object order", 2, 0, [s](Ctx& c) {
        const Word order = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        SystemState::Plane* p = plane_of(*s, c, handle);
        if (!p) return 0;
        if (order > 5) {
            c.fail(Errc::Range, "window object order " + std::to_string(order) + " is not in 0..5");
            return 0;
        }
        p->order = order;
        return 0;
    });
    // (image slot, handle): captures the window object as it is drawn into a new image slot of its size (the original creates
    // the bitmap and has the object render itself into it)
    add(r, 0x9083, "window object bitmap", 2, 0, [s](Ctx& c) {
        const Word handle = c.pop(), image = c.pop();
        if (!c.ok()) return 0;
        debug_object_op(0x9083, {handle, image});
        SystemState::Plane* p = plane_of(*s, c, handle);
        if (!p) return 0;
        p->extra = {image};
        Display* d = s->display;
        const Word plane_slot = kPlaneSlotBase + (handle & 0xFFFFFFu);
        if (d && image < 0x4000 && d->slot(plane_slot) && p->width && p->height) {
            auto made = d->create_slot(image, p->width, p->height, SlotFormat::Bgra);
            if (!made) { c.fail(made.error().code, made.error().message); return 0; }
            if (auto copy = d->blit(image, 0, 0, plane_slot, 0, 0, p->width, p->height, 0, 0x100); !copy) {
                c.fail(copy.error().code, copy.error().message); return 0;
            }
            // Panels are drawn as separate sprites in this runtime, but the
            // original window capture includes its painted items/sub-sprites.
            for (const auto& [id, panel] : s->icon_controls) {
                if (panel.window != handle) continue;
                for (const auto& group : panel.groups) for (const auto& button : group.buttons) {
                    if (!button.shown || !button.sprite) continue;
                    const Sprite* sp = d->find_sprite(button.sprite);
                    const SlotInfo* info = sp ? d->slot(sp->slot) : nullptr;
                    if (!info) continue;
                    const auto x = button.x + (panel.basic ? p->area[0] : 0);
                    const auto y = button.y + (panel.basic ? p->area[1] : 0);
                    if (auto drawn = d->blit(image, x, y, sp->slot, 0, 0, info->width, info->height, 1, 0); !drawn) {
                        c.fail(drawn.error().code, drawn.error().message); return 0;
                    }
                }
            }
        }
        return 0;
    });
    // (handle, value)
    add(r, 0x9084, "window object flag", 2, 0, [s](Ctx& c) {
        const Word value = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle)) {
            p->flag = value;
            s->plane_shown[handle & 0xFFFFFFu] = value != 0;
            if (s->display) s->display->set_visible(handle, value != 0);
        }
        return 0;
    });
    // (handle, x, y, a, b, c, d): position and display parameters. The first two numbers after the handle are x, y.
    add(r, 0x9085, "window object placement", 7, 0, [s](Ctx& c) {
        std::array<Word, 7> a{};
        for (int i = 6; i >= 0; --i) a[std::size_t(i)] = c.pop();
        if (!c.ok()) return 0;
        if (a[6] > 0xFFFF) {
            c.fail(Errc::Range, "window object placement: " + std::to_string(a[6]) + " is above 65535");
            return 0;
        }
        debug_object_op(0x9085, std::vector<Word>(a.begin(), a.end()));
        if (SystemState::Plane* p = plane_of(*s, c, a[0])) {
            p->x = static_cast<std::int32_t>(a[1]);
            p->y = static_cast<std::int32_t>(a[2]);
            p->extra.assign(a.begin() + 3, a.end());
            if (Sprite* sp = s->display ? s->display->find_sprite(a[0]) : nullptr) { // the window object's own place and draw order
                sp->x = p->x, sp->y = p->y;
                sp->priority = a[6];
                sp->alpha = std::min<Word>(a[4], 0x100); // the window's own transparency
            }
            s->plane_effect[a[0] & 0xFFFFFFu] = a[3], s->plane_level[a[0] & 0xFFFFFFu] = a[4];
        }
        return 0;
    });
    // (handle, value)
    add(r, 0x9087, "window object mode", 2, 0, [s](Ctx& c) {
        const Word value = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle)) p->mode = value;
        return 0;
    });
    // (handle, x, y, width, height): the plane's active area
    add(r, 0x9088, "window object area", 5, 0, [s](Ctx& c) {
        const std::int32_t h = std::int32_t(c.pop()), w = std::int32_t(c.pop()), y = std::int32_t(c.pop()), x = std::int32_t(c.pop());
        const Word handle = c.pop();
        if (!c.ok()) return 0;
        debug_object_op(0x9088, {handle, static_cast<Word>(x), static_cast<Word>(y), static_cast<Word>(w), static_cast<Word>(h)});
        if (SystemState::Plane* p = plane_of(*s, c, handle)) p->area = {x, y, x + w - 1, y + h - 1};
        return 0;
    });
    // (address, handle): writes the active area (left, top, right, bottom) to the address; pushes 1
    add(r, 0x9089, "window object area read", 2, 1, [s](Ctx& c) {
        const Word handle = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle)) {
            for (std::size_t i = 0; i < 4; ++i) c.write(dst + Word(i * 4), static_cast<Word>(p->area[i]), 4);
            c.push(1);
        }
        return 0;
    });
}

} // namespace obgi
