// Script window planes (90:80..89). The original keeps up to 16 window objects (FUN_004486d0) and hands the scripts
// the handle 0xB0000000 + slot. Nothing draws them yet; the state a display object needs later is kept in
// SystemState::planes. Analysis: analysis/子opcode语义表.md and the handlers FUN_0048cdb0..0048d240.
#include <algorithm>
#include <cmath>
#include <memory>
#include <cstdio>
#include <cstdlib>

#include "bindings/panel.hpp"
#include "bindings/system_subops.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

constexpr Word kHandleBase = 0xB0000000u;
constexpr Word kPlaneSlotBase = 0x10000u; // image slots above the scripts' own range (0..0x3fff)

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
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
    static const Rec recs[] = {{0x9131, "object setting 91:31", 2, 0}, {0x9133, "object setting 91:33", 4, 0},
                               {0x9136, "object setting 91:36", 4, 0}, {0x9137, "object setting 91:37", 4, 0},
                               {0x9140, "object stretch 91:40", 9, 0}, {0x9141, "object setting 91:41", 1, 0},
                               {0x9142, "object setting 91:42", 2, 0}, {0x9143, "object setting 91:43", 3, 0},
                               {0x9144, "object setting 91:44", 2, 0}, {0x9145, "object setting 91:45", 2, 0},
                               {0x9146, "object setting 91:46", 4, 0}, {0x9147, "object setting 91:47", 5, 0},
                               {0x9148, "object layer 91:48", 3, 0}, {0x9149, "object layer 91:49", 3, 0},
                               {0x914A, "object layer 91:4A", 4, 0}, {0x9155, "object query 91:55", 2, 1},
                               {0x913D, "object query 91:3D", 1, 1}, {0x9138, "object parameter read 91:38", 3, 0}};
    // 90:3x..5x: settings kept per op; those the analysis says take an object first check that it exists
    static const Rec objrecs[] = {{0x9031, "object setting 90:31", 2, 0}, {0x9035, "object setting 90:35", 2, 0},
                                  {0x9036, "object setting 90:36", 3, 0}, {0x9037, "object setting 90:37", 3, 0}};
    for (const Rec& rec : objrecs) {
        const std::uint16_t id = rec.id;
        const int pops = rec.pops;
        add(r, id, rec.name, pops, 0, [s, display, id, pops](Ctx& c) {
            std::vector<Word> a(static_cast<std::size_t>(pops));
            for (int i = pops - 1; i >= 0; --i) a[std::size_t(i)] = c.pop();
            if (!c.ok()) return 0;
            debug_object_op(id, a);
            if (a[0] != 0 && display && !display->find_sprite(a[0])) { // id 0 is the root (screen) object, which always exists
                c.fail(Errc::Range, "no display object " + std::to_string(a[0]));
                return 0;
            }
            s->records[id] = std::move(a);
            return 0;
        });
    }
    static const Rec loose[] = {{0x9039, "object setting 90:39", 2, 0}, {0x903C, "object setting 90:3C", 2, 0},
                                {0x903D, "object query 90:3D", 1, 1},   {0x903F, "object setting 90:3F", 1, 0},
                                {0x9041, "object setting 90:41", 3, 0},
                                {0x9042, "object setting 90:42", 6, 0}, {0x9044, "background frames 90:44", 2, 0},
                                {0x9045, "object setting 90:45", 5, 0}, {0x9046, "object setting 90:46", 3, 0},
                                {0x9048, "object setting 90:48", 5, 0}, {0x9049, "object setting 90:49", 3, 0},
                                {0x9053, "object setting 90:53", 5, 0}, {0x9055, "object setting 90:55", 2, 0}};
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
        g->x = x, g->y = y, g->alpha = level;
        for (Word m : members(h)) display->find_sprite(m)->alpha = level;
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
        o->visible = g->visible;
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
    // Effectors (91:60..66): up to 8 screen effect objects, handle 0x91000000 | index. Only the bookkeeping is modelled; their
    // displacement / gradient effects on the screen below are not drawn.
    auto effectors = std::make_shared<std::array<bool, 8>>();
    auto effector_of = [effectors](Ctx& c, Word h) {
        const bool ok = (h & 0xFFFFFFF8u) == 0x91000000u && (*effectors)[h & 7];
        if (!ok) c.fail(Errc::Range, "no effector " + std::to_string(h));
        return ok;
    };
    add(r, 0x9160, "create effector", 0, 1, [effectors](Ctx& c) {
        for (Word i = 0; i < 8; ++i)
            if (!(*effectors)[i]) { (*effectors)[i] = true; c.push(0x91000000u | i); return 0; }
        c.fail(Errc::Range, "no more effectors (the limit is 8)");
        return 0;
    });
    add(r, 0x9161, "delete effector", 1, 0, [effectors, effector_of](Ctx& c) {
        const Word h = c.pop();
        if (c.ok() && effector_of(c, h)) (*effectors)[h & 7] = false;
        return 0;
    });
    add(r, 0x9164, "show effector", 2, 0, [effector_of](Ctx& c) {
        c.pop();
        const Word h = c.pop();
        if (c.ok()) effector_of(c, h);
        return 0;
    });
    for (auto [id, n] : {std::pair{0x9165, 6}, {0x9166, 4}, {0x9167, 6}, {0x9168, 9}})
        add(r, static_cast<std::uint16_t>(id), "set effector (not drawn)", n, 0, [effector_of, n](Ctx& c) {
            std::vector<Word> a(static_cast<std::size_t>(n));
            for (int i = n - 1; i >= 0; --i) a[std::size_t(i)] = c.pop();
            if (c.ok()) effector_of(c, a[0]);
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
std::int32_t ease(std::int32_t t, int curve) {
    constexpr double deg16_to_rad = 2.663161090079238e-07;
    constexpr double powers[6] = {2.0, 2.5, 3.0, 4.0, 5.0, 6.0};
    switch (curve) {
    case 1: return static_cast<std::int32_t>((std::cos((0xB40000 - std::int64_t{t} * 180 / 256) * deg16_to_rad) + 1.0) * 32768.0);
    case 2: return static_cast<std::int32_t>(std::sin((std::int64_t{t} * 90 / 256) * deg16_to_rad) * 65536.0);
    case 3: return static_cast<std::int32_t>((1.0 - std::sin((0x5A0000 - std::int64_t{t} * 90 / 256) * deg16_to_rad)) * 65536.0);
    default:
        if (curve >= 4 && curve <= 15) {
            const double n = powers[(curve - 4) / 2];
            if (curve & 1) return static_cast<std::int32_t>((1.0 - std::pow(double(0x1000000 - t), n) / std::pow(16777216.0, n)) * 65536.0);
            return static_cast<std::int32_t>(std::pow(double(t), n) * 65536.0 / std::pow(16777216.0, n));
        }
        return t / 256;
    }
}

// Starts a timed change of display object `id`: to (x, y) along `curve` and to alpha `level` along `level_curve`, over
// `duration` ms. The object becomes visible; scripts wait for the end with 80:5C. `move` = false leaves the position alone.
void start_motion(SystemState& s, Display& display, Ctx& c, Word id, bool move, std::int32_t x, std::int32_t y, int curve, Word level,
                  int level_curve, Word duration) {
    Sprite* sp = display.find_sprite(id);
    if (!sp && id >= 0x90000000u) return; // filters, effectors, windows and the like are not drawn objects here
    if (!sp) { c.fail(Errc::Range, "no display object " + std::to_string(id)); return; }
    SystemState::Motion m;
    m.object = id;
    m.start_ms = c.vm.now_ms;
    m.duration_ms = duration;
    m.from_x = sp->x, m.from_y = sp->y;
    m.to_x = move ? x : sp->x, m.to_y = move ? y : sp->y;
    m.curve = curve;
    m.from_level = static_cast<std::int32_t>(sp->alpha), m.to_level = static_cast<std::int32_t>(level);
    m.level_curve = level_curve;
    sp->visible = true;
    if (std::getenv("OBGI_DBGOBJ"))
        std::fprintf(stderr, "motion object %X: %d,%d -> %d,%d level %d -> %d over %u ms\n", id, m.from_x, m.from_y, m.to_x, m.to_y, m.from_level, m.to_level, duration);
    std::erase_if(s.motions, [&](const auto& o) { return o.object == id; });
    if (m.duration_ms <= 0) {
        if (move) sp->move_to(m.to_x, m.to_y);
        sp->alpha = level;
        return;
    }
    s.motions.push_back(m);
    c.t.objects_until = std::max(c.t.objects_until, m.start_ms + m.duration_ms);
}

} // namespace

void advance_motions(SystemState& state, Display& display, std::int64_t now_ms) {
    for (auto it = state.motions.begin(); it != state.motions.end();) {
        Sprite* sp = display.find_sprite(it->object);
        const std::int64_t elapsed = std::min(now_ms - it->start_ms, it->duration_ms);
        if (sp && it->duration_ms > 0) {
            const bool done = elapsed >= it->duration_ms;
            const auto t = static_cast<std::int32_t>(elapsed * 0x1000000 / it->duration_ms);
            const std::int64_t fp = done ? 0x10000 : ease(t, it->curve), fl = done ? 0x10000 : ease(t, it->level_curve);
            sp->move_to(it->from_x + static_cast<std::int32_t>((std::int64_t{it->to_x} - it->from_x) * fp >> 16),
                        it->from_y + static_cast<std::int32_t>((std::int64_t{it->to_y} - it->from_y) * fp >> 16));
            sp->alpha = static_cast<std::uint32_t>(it->from_level + static_cast<std::int32_t>((std::int64_t{it->to_level} - it->from_level) * fl >> 16));
            if (sp->slot == 0xFFFFFFFFu) // a group (90:E0): its members take its level
                for (auto& [mid, m] : display.sprites())
                    if (m.has_master && m.master == it->object) m.alpha = sp->alpha;
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
    add_plane_setting(r, state, 0x918B, "window object text sway", 2);
    add_plane_setting(r, state, 0x918C, "window object setting 91:8C", 3);
    add_plane_setting(r, state, 0x9288, "window object setting 92:88", 2);
    add_plane_setting(r, state, 0x9289, "window object background bitmap", 6);
    add_plane_setting(r, state, 0x928C, "window object setting 92:8C", 2);
    add_plane_setting(r, state, 0x928E, "window object setting 92:8E", 1);
    // (handle): pushes ok, and two numbers; what they are is not known (zeros for now)
    add(r, 0x918D, "window object query 91:8D", 1, 3, [s](Ctx& c) {
        const Word handle = c.pop();
        if (!c.ok() || !plane_of(*s, c, handle)) return 0;
        c.push(1), c.push(0), c.push(0);
        return 0;
    });
    // 90:28, popped in this order: priority, input-skip flag, total, rate, step, three more values (alpha-like, not
    // modelled), a value, y, x, object. The object moves to (x, y) linearly over total * 1000 / rate ms; 80:5C waits for it.
    // (analysis: FUN_0048a0e0 -> FUN_004a1070 -> CProcCtrlDspObj FUN_00437d60). A rate of 0 or a missing object is a script error.
    // Popped in this order: prio, skip, max skip, fps, duration (ms), progress, level curve, level, curve, y, x, object (the
    // reference engine's layout, docs/vm.md of the other project). A rate of 0 or a missing object is a script error.
    if (display) add(r, 0x9028, "move object over time", 12, 0, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        if (a[3] == 0) { c.fail(Errc::Range, "90:28: rate 0"); return 0; }
        start_motion(*s, *display, c, a[11], true, static_cast<std::int32_t>(a[10]), static_cast<std::int32_t>(a[9]), static_cast<int>(a[8]), a[7],
                     static_cast<int>(a[6]), a[4]);
        return 0;
    });
    // 90:20 fade: prio, skip, fps, duration, level, object. 90:22 adds max skip after skip.
    // 90:21 move: prio, skip, fps, duration, level, curve, y, x, object. 90:23 adds max skip after skip.
    if (display) {
        for (auto [id, n] : {std::pair{0x9020, 6}, {0x9022, 7}, {0x9021, 9}, {0x9023, 10}}) {
            const bool move = id == 0x9021 || id == 0x9023;
            const bool has_max = id == 0x9022 || id == 0x9023;
            add(r, static_cast<std::uint16_t>(id), move ? "move object (timed)" : "fade object (timed)", n, 0, [s, display, n, move, has_max](Ctx& c) {
                std::vector<Word> a(static_cast<std::size_t>(n));
                for (Word& w : a) w = c.pop();
                if (!c.ok()) return 0;
                const std::size_t i = has_max ? 3 : 2; // index of fps
                if (a[i] == 0) { c.fail(Errc::Range, "invalid frame rate"); return 0; }
                if (a[i + 2] > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
                if (move)
                    start_motion(*s, *display, c, a[std::size_t(n) - 1], true, static_cast<std::int32_t>(a[std::size_t(n) - 2]),
                                 static_cast<std::int32_t>(a[std::size_t(n) - 3]), static_cast<int>(a[std::size_t(n) - 4]), a[i + 2], 0, a[i + 1]);
                else
                    start_motion(*s, *display, c, a[std::size_t(n) - 1], false, 0, 0, 0, a[i + 2], 0, a[i + 1]);
                return 0;
            });
        }
    }
    // 90:29: the same over a path of n points (four 16.16 words each: x, y, z, padding); popped in this order: priority, skip,
    // max skip, fps, duration (frames), progress, level curve, level, curve, point address, n, object. Simplified: the object
    // moves straight to the last point; the intermediate points and the curve are not followed yet.
    if (display) add(r, 0x9029, "move object along a path", 12, 0, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        const Word id = a[11], n = a[10], points = a[9], duration = a[4], fps = a[3];
        if (fps == 0 || n == 0) { c.fail(Errc::Range, "90:29: bad frame rate or point count"); return 0; }
        const Word last = points + 16 * (n - 1);
        const auto to_x = static_cast<std::int32_t>(c.read(last, 4, false)) >> 16;
        const auto to_y = static_cast<std::int32_t>(c.read(last + 4, 4, false)) >> 16;
        if (!c.ok()) return 0;
        start_motion(*s, *display, c, id, true, to_x, to_y, static_cast<int>(a[8]), a[7], static_cast<int>(a[6]), duration);
        return 0;
    });
    // 90:24: through a via point; popped: prio, skip, max skip, fps, duration, level, curve, y, x, via y, via x, object.
    // Simplified: straight to (x, y).
    if (display) add(r, 0x9024, "move object through a point", 12, 0, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        if (a[3] == 0) { c.fail(Errc::Range, "invalid frame rate"); return 0; }
        start_motion(*s, *display, c, a[11], true, static_cast<std::int32_t>(a[8]), static_cast<std::int32_t>(a[7]), static_cast<int>(a[6]), a[5], 0, a[4]);
        return 0;
    });
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
        }
        s->planes[slot] = std::move(p);
        c.push(kHandleBase + static_cast<Word>(slot));
        return 0;
    });
    // (handle): removes a plane
    add(r, 0x9081, "remove window object", 1, 0, [s, display](Ctx& c) {
        const Word handle = c.pop();
        if (!c.ok() || !plane_of(*s, c, handle)) return 0;
        if (display) {
            display->remove_sprite(handle);
            display->free_slot(kPlaneSlotBase + (handle & 0xFFFFFFu));
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
            if (made) (void)d->blit(image, 0, 0, plane_slot, 0, 0, p->width, p->height, 0, 0x100);
        }
        return 0;
    });
    // (handle, value)
    add(r, 0x9084, "window object flag", 2, 0, [s](Ctx& c) {
        const Word value = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle)) p->flag = value;
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
            }
        }
        return 0;
    });
    // (handle, ?, ?, bitmap id): the plane's border bitmap, -1 clears it
    add(r, 0x9086, "window object border", 4, 0, [s](Ctx& c) {
        const Word image = c.pop(), b = c.pop(), a = c.pop(), handle = c.pop();
        if (!c.ok()) return 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle)) {
            p->image = image;
            p->extra = {a, b};
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
