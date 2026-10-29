// Script window planes (90:80..89). The original keeps up to 16 window objects (FUN_004486d0) and hands the scripts
// the handle 0xB0000000 + slot. Nothing draws them yet; the state a display object needs later is kept in
// SystemState::planes. Analysis: analysis/子opcode语义表.md and the handlers FUN_0048cdb0..0048d240.
#include <algorithm>

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
                                  {0x9036, "object setting 90:36", 3, 0}, {0x9037, "object setting 90:37", 3, 0},
                                  {0x903A, "object setting 90:3A", 2, 0}};
    for (const Rec& rec : objrecs) {
        const std::uint16_t id = rec.id;
        const int pops = rec.pops;
        add(r, id, rec.name, pops, 0, [s, display, id, pops](Ctx& c) {
            std::vector<Word> a(static_cast<std::size_t>(pops));
            for (int i = pops - 1; i >= 0; --i) a[std::size_t(i)] = c.pop();
            if (!c.ok()) return 0;
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
                                {0x9040, "object setting 90:40", 1, 0}, {0x9041, "object setting 90:41", 3, 0},
                                {0x9042, "object setting 90:42", 6, 0}, {0x9044, "object query 90:44", 2, 1},
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
            s->records[id] = std::move(a);
            for (int i = 0; i < pushes; ++i) c.push(0);
            return 0;
        });
    }
    // (slave, master, dx, dy): both objects must exist and differ
    add(r, 0x913E, "bind slave object", 4, 0, [display](Ctx& c) {
        const auto dy = static_cast<std::int32_t>(c.pop()), dx = static_cast<std::int32_t>(c.pop());
        const Word master = c.pop(), slave = c.pop();
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
        return 0;
    });
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
        if (SystemState::Plane* p = plane_of(*st, c, a[0])) p->props[id].assign(a.begin() + 1, a.end());
        return 0;
    });
}

} // namespace

void advance_motions(SystemState& state, Display& display, std::int64_t now_ms) {
    for (auto it = state.motions.begin(); it != state.motions.end();) {
        Sprite* sp = display.find_sprite(it->object);
        const std::int64_t elapsed = std::min(now_ms - it->start_ms, it->duration_ms);
        if (sp && it->duration_ms > 0) {
            sp->x = it->from_x + static_cast<std::int32_t>((std::int64_t{it->to_x} - it->from_x) * elapsed / it->duration_ms);
            sp->y = it->from_y + static_cast<std::int32_t>((std::int64_t{it->to_y} - it->from_y) * elapsed / it->duration_ms);
        }
        it = (!sp || now_ms - it->start_ms >= it->duration_ms) ? state.motions.erase(it) : it + 1;
    }
    panel_sync(state);
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
    if (display) add(r, 0x9028, "move object over time", 12, 0, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        const Word total = a[2], rate = a[3], y = a[9], x = a[10], id = a[11];
        if (rate == 0) { c.fail(Errc::Range, "90:28: rate 0"); return 0; }
        Sprite* sp = display->find_sprite(id);
        if (!sp && id != 0) { c.fail(Errc::Range, "no display object " + std::to_string(id)); return 0; }
        if (!sp) return 0;
        SystemState::Motion m;
        m.object = id;
        m.start_ms = c.vm.now_ms;
        m.duration_ms = std::int64_t{total} * 1000 / rate;
        m.from_x = sp->x, m.from_y = sp->y;
        m.to_x = static_cast<std::int32_t>(x), m.to_y = static_cast<std::int32_t>(y);
        std::erase_if(s->motions, [&](const auto& o) { return o.object == id; });
        if (m.duration_ms <= 0) {
            sp->x = m.to_x, sp->y = m.to_y;
            return 0;
        }
        s->motions.push_back(m);
        c.t.objects_until = std::max(c.t.objects_until, m.start_ms + m.duration_ms);
        return 0;
    });
    // 90:29: the same over a path of n points (four 16.16 words each: x, y, z, padding); popped in this order: priority, skip,
    // max skip, fps, duration (frames), progress, level curve, level, curve, point address, n, object. Simplified: the object
    // moves straight to the last point; the intermediate points and the curve are not followed yet.
    if (display) add(r, 0x9029, "move object along a path", 12, 0, [s, display](Ctx& c) {
        Word a[12];
        for (Word& w : a) w = c.pop();
        if (!c.ok()) return 0;
        const Word id = a[11], n = a[10], points = a[9], duration = a[4], fps = a[3];
        if (fps == 0 || n == 0) { c.fail(Errc::Range, "90:29: bad frame rate or point count"); return 0; }
        Sprite* sp = display->find_sprite(id);
        if (!sp) { c.fail(Errc::Range, "no display object " + std::to_string(id)); return 0; }
        const Word last = points + 16 * (n - 1);
        SystemState::Motion m;
        m.object = id;
        m.start_ms = c.vm.now_ms;
        m.duration_ms = std::int64_t{duration} * 1000 / fps;
        m.from_x = sp->x, m.from_y = sp->y;
        m.to_x = static_cast<std::int32_t>(c.read(last, 4, false)) >> 16;
        m.to_y = static_cast<std::int32_t>(c.read(last + 4, 4, false)) >> 16;
        if (!c.ok()) return 0;
        std::erase_if(s->motions, [&](const auto& o) { return o.object == id; });
        if (m.duration_ms <= 0) {
            sp->x = m.to_x, sp->y = m.to_y;
            return 0;
        }
        s->motions.push_back(m);
        c.t.objects_until = std::max(c.t.objects_until, m.start_ms + m.duration_ms);
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
    // (bitmap id, handle): fits the bitmap to the plane's size (needs the image manager; not drawn yet)
    add(r, 0x9083, "window object bitmap", 2, 0, [s](Ctx& c) {
        const Word handle = c.pop(), image = c.pop();
        if (!c.ok()) return 0;
        if (SystemState::Plane* p = plane_of(*s, c, handle)) p->extra = {image};
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
