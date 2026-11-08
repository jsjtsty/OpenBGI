#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "bindings/system_subops.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;
using Knob = SystemState::Knob;

constexpr Word kHandleBase = 0xF0000000u;
constexpr std::size_t kMaxKnobs = 0x20;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, std::function<int(Ctx&)> fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

Knob* knob_of(SystemState& s, Ctx& c, Word handle) {
    auto it = s.knobs.find(handle);
    if (it == s.knobs.end()) {
        c.fail(Errc::Range, "not a knob control: " + std::to_string(handle));
        return nullptr;
    }
    return &it->second;
}

// The thumb's size: its image, at least 1 x 1.
std::pair<std::int32_t, std::int32_t> thumb_size(const SystemState& s, const Knob& k) {
    if (s.display)
        if (Sprite* sp = s.display->find_sprite(k.target))
            if (const SlotInfo* si = s.display->slot(sp->slot)) return {std::max<std::int32_t>(1, static_cast<std::int32_t>(si->width)), std::max<std::int32_t>(1, static_cast<std::int32_t>(si->height))};
    return {1, 1};
}

// The thumb's travel on an axis: (track - 1) - (thumb - 1) pixels.
std::int32_t travel(std::int32_t track, std::int32_t thumb) { return std::max(track - thumb, 0); }

// Discrete values are step indices. The original computes a 16.16 pixel distance per step.
std::int32_t step_size(std::int32_t track, std::int32_t thumb, std::int32_t steps) {
    std::int64_t size = std::int64_t{travel(track, thumb)} << 16;
    if (steps > 2) size /= steps - 1;
    return static_cast<std::int32_t>(std::max<std::int64_t>(size, 1));
}

std::int32_t max_value(std::int32_t track, std::int32_t thumb, std::int32_t steps) {
    const auto room = travel(track, thumb);
    if (steps < 1) return room;
    if (steps == 1) return 0;
    return static_cast<std::int32_t>((std::int64_t{room} << 16) / step_size(track, thumb, steps));
}

std::int32_t to_pixels(std::int32_t value, std::int32_t track, std::int32_t thumb, std::int32_t steps) {
    return steps > 0 ? static_cast<std::int32_t>((std::int64_t{step_size(track, thumb, steps) + 1} * value) >> 16) : value;
}

// The knob's own position: its display object (the scripts move it with the 90:3x object ops too).
std::pair<std::int32_t, std::int32_t> base_of(const SystemState& s, const Knob& k) {
    if (s.display)
        if (Sprite* ks = s.display->find_sprite(k.handle)) return {ks->x, ks->y};
    return {0, 0};
}

// The thumb is the knob's slave at the value's offset.
void place(SystemState& s, const Knob& k) {
    if (!s.display) return;
    Sprite* sp = s.display->find_sprite(k.target);
    if (!sp) return;
    const auto [w, h] = thumb_size(s, k);
    const auto [bx, by] = base_of(s, k);
    sp->has_master = true, sp->master = k.handle;
    sp->master_dx = to_pixels(k.value_x, k.track_w, w, k.steps_x), sp->master_dy = to_pixels(k.value_y, k.track_h, h, k.steps_y);
    sp->x = bx + sp->master_dx, sp->y = by + sp->master_dy;
    sp->moved = false;
}

// 90:D6: each axis keeps its value when it is in range; the thumb moves only when both are.
void set_value(SystemState& s, Knob& k, std::int32_t vx, std::int32_t vy) {
    const auto [w, h] = thumb_size(s, k);
    const bool ok_x = vx >= 0 && vx <= max_value(k.track_w, w, k.steps_x), ok_y = vy >= 0 && vy <= max_value(k.track_h, h, k.steps_y);
    if (ok_x) k.value_x = vx;
    if (ok_y) k.value_y = vy;
    if (ok_x && ok_y) place(s, k);
}

// The value the pointer asks for on one axis while dragging.
std::int32_t dragged(std::int32_t pointer, std::int32_t grab, std::int32_t base, std::int32_t track, std::int32_t thumb, std::int32_t steps) {
    const std::int32_t room = travel(track, thumb);
    const std::int32_t px = std::clamp(pointer - grab - base, 0, room);
    if (steps < 1) return px;
    const std::int32_t half = steps < 2 ? 0 : room / (steps * 2 - 2);
    return static_cast<std::int32_t>(((std::int64_t{half} + px) << 16) / step_size(track, thumb, steps));
}

bool hit(const SystemState& s, const Knob& k) {
    if (!s.display) return false;
    Sprite* sp = s.display->find_sprite(k.target);
    if (!sp || !sp->visible || !k.enabled) return false;
    const auto [w, h] = thumb_size(s, k);
    return s.mouse_x >= sp->x && s.mouse_x < sp->x + w && s.mouse_y >= sp->y && s.mouse_y < sp->y + h;
}

// Knob handles, highest object priority first (the one drawn on top wins a click).
std::vector<Word> by_priority(const SystemState& s) {
    std::vector<Word> order;
    for (const auto& [h, k] : s.knobs) order.push_back(h);
    auto prio = [&](Word h) {
        Sprite* sp = s.display ? s.display->find_sprite(s.knobs.at(h).target) : nullptr;
        return sp ? sp->priority : 0u;
    };
    std::stable_sort(order.begin(), order.end(), [&](Word a, Word b) { return prio(a) > prio(b); });
    return order;
}

} // namespace

void knob_input(SystemState& s) {
    // A captured knob takes each wheel event; otherwise it becomes the engine's wheel-up/down pseudo key.
    while (!s.wheel_events.empty()) {
        const int step = s.wheel_events.front();
        s.wheel_events.pop_front();
        const bool away = step > 0;
        auto mode = s.values.find(0x90DD);
        const bool scrolls = (mode == s.values.end() || mode->second != 0) && !s.knob_wheel.empty();
        if (!scrolls) {
            s.press_input(away ? 0x0E : 0x0F, false); // the host already bumped the serial for this wheel event
            continue;
        }
        Knob& k = s.knobs.at(s.knob_wheel.front());
        const std::int32_t dir = away ? -1 : 1;
        const std::int32_t h = thumb_size(s, k).second;
        k.wheel_step = dir;
        const std::int32_t vy = k.value_y + dir;
        if (vy >= 0 && vy <= max_value(k.track_h, h, k.steps_y)) set_value(s, k, k.value_x, vy);
    }
    if (s.knobs.empty()) {
        s.knob_focus = 0;
        s.knob_seen_left = s.input_counts[1], s.knob_seen_right = s.input_counts[2];
        return;
    }
    if (s.input_counts[1] != s.knob_seen_left) {
        s.knob_seen_left = s.input_counts[1];
        for (Word h : by_priority(s))
            if (hit(s, s.knobs.at(h))) {
                Knob& k = s.knobs.at(h);
                s.knob_focus = h;
                Sprite* sp = s.display->find_sprite(k.target);
                k.grab_x = k.enabled ? s.mouse_x - sp->x : 0;
                k.grab_y = k.enabled ? s.mouse_y - sp->y : 0;
                break;
            }
    }
    if (s.input_counts[2] != s.knob_seen_right) {
        s.knob_seen_right = s.input_counts[2];
        for (Word h : by_priority(s))
            if (hit(s, s.knobs.at(h))) {
                s.knobs.at(h).flagged = true;
                break;
            }
    }
    if (s.knob_focus) {
        auto it = s.knobs.find(s.knob_focus);
        if (it == s.knobs.end() || !s.left_down) {
            s.knob_focus = 0;
            return;
        }
        Knob& k = it->second;
        const auto [w, h] = thumb_size(s, k);
        const auto [bx, by] = base_of(s, k);
        const std::int32_t vx = dragged(s.mouse_x, k.grab_x, bx, k.track_w, w, k.steps_x);
        const std::int32_t vy = dragged(s.mouse_y, k.grab_y, by, k.track_h, h, k.steps_y);
        if (vx != k.value_x || vy != k.value_y) {
            set_value(s, k, vx, vy);
            if (std::getenv("OBGI_DBGKNOB")) {
                Sprite* t = s.display->find_sprite(k.target);
                std::fprintf(stderr, "knob %X dragged to %d,%d (steps %d,%d track %d,%d thumb %d,%d) target %X at %d,%d visible %d base %d,%d\n", it->first, vx, vy, k.steps_x, k.steps_y, k.track_w, k.track_h, w, h, k.target, t ? t->x : -1, t ? t->y : -1, t ? t->visible : -1, bx, by);
            }
        }
    }
}

void register_knob_subops(vm::SubopRegistry& r, SystemState& state) {
    SystemState* s = &state;

    // 90:D0 (object): makes a knob of a display object, pushes its handle. The thumb starts where the object is.
    add(r, 0x90D0, "knob create", 1, 1, [s](Ctx& c) {
        const Word object = c.pop();
        if (!c.ok()) return 0;
        if ((object & 0xFF000000u) == kHandleBase) {
            c.fail(Errc::Range, "the object is a knob already");
            return 0;
        }
        if (s->knobs.size() >= kMaxKnobs) {
            c.fail(Errc::Range, "too many knobs");
            return 0;
        }
        Sprite* sp = s->display ? s->display->find_sprite(object) : nullptr;
        if (!sp) {
            c.fail(Errc::Range, "no display object " + std::to_string(object));
            return 0;
        }
        Word index = 0;
        while (s->knobs.count(kHandleBase + index)) ++index;
        Knob k;
        k.handle = kHandleBase + index;
        k.target = object;
        k.priority = sp->priority;
        const auto [w, h] = thumb_size(*s, k);
        k.track_w = w, k.track_h = h;
        // the knob object sits where the thumb is, draws nothing, and has the thumb as its child
        Sprite& ks = s->display->sprite(k.handle);
        ks = Sprite{};
        ks.slot = 0xFFFFFFFFu;
        ks.x = sp->x, ks.y = sp->y;
        ks.priority = sp->priority;
        ks.visible = sp->visible;
        s->knobs[k.handle] = k;
        place(*s, k);
        c.push(k.handle);
        return 0;
    });
    // 90:D1 (knob)
    add(r, 0x90D1, "knob destroy", 1, 0, [s](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (!knob_of(*s, c, h)) return 0;
        s->knob_wheel.erase(std::remove(s->knob_wheel.begin(), s->knob_wheel.end(), h), s->knob_wheel.end());
        if (s->knob_focus == h) s->knob_focus = 0;
        if (s->display) {
            if (Sprite* t = s->display->find_sprite(s->knobs.at(h).target)) t->has_master = false;
            s->display->remove_sprite(h);
        }
        s->knobs.erase(h);
        return 0;
    });
    // 90:D4 (knob, visible): shows or hides the thumb
    add(r, 0x90D4, "knob visible", 2, 0, [s](Ctx& c) {
        const Word on = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (Knob* k = knob_of(*s, c, h))
            if (s->display) s->display->set_visible(k->target, on != 0);
        return 0;
    });
    // 90:D5 (knob, x, y): moves the knob; the thumb keeps its value
    add(r, 0x90D5, "knob position", 3, 0, [s](Ctx& c) {
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (Knob* k = knob_of(*s, c, h)) {
            if (s->display)
                if (Sprite* ks = s->display->find_sprite(h)) ks->x = x, ks->y = y;
            place(*s, *k);
        }
        return 0;
    });
    // 90:D6 (knob, x, y): sets the value
    add(r, 0x90D6, "knob set value", 3, 0, [s](Ctx& c) {
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (Knob* k = knob_of(*s, c, h)) set_value(*s, *k, x, y);
        return 0;
    });
    // 90:D7 (knob): pushes the value x, then y
    add(r, 0x90D7, "knob get value", 1, 2, [s](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Knob* k = knob_of(*s, c, h);
        if (!k) return 0;
        c.push(static_cast<Word>(k->value_x));
        c.push(static_cast<Word>(k->value_y));
        return 0;
    });
    // 90:D8 (knob, x steps, y steps): 0 = pixel values, n = n discrete positions; negative counts are errors
    add(r, 0x90D8, "knob steps", 3, 0, [s](Ctx& c) {
        const auto sy = static_cast<std::int32_t>(c.pop()), sx = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Knob* k = knob_of(*s, c, h);
        if (!k) return 0;
        if (sx < 0 || sy < 0) {
            c.fail(Errc::Range, "bad knob step count " + std::to_string(sx < 0 ? sx : sy));
            return 0;
        }
        k->steps_x = sx, k->steps_y = sy;
        return 0;
    });
    // 90:D9 (knob, width, height): the track; smaller than the thumb is ignored. The value goes back to 0.
    add(r, 0x90D9, "knob track", 3, 0, [s](Ctx& c) {
        const auto th = static_cast<std::int32_t>(c.pop()), tw = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Knob* k = knob_of(*s, c, h);
        if (!k) return 0;
        const auto [w, hh] = thumb_size(*s, *k);
        if (tw >= w && th >= hh) k->track_w = tw, k->track_h = th;
        return 0;
    });
    // 90:DA (knob): the wheel step the knob was scrolled by since the last call (+1 towards the user, -1 away, 0 none)
    add(r, 0x90DA, "knob wheel step", 1, 1, [s](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Knob* k = knob_of(*s, c, h);
        if (!k) return 0;
        c.push(static_cast<Word>(std::exchange(k->wheel_step, 0)));
        return 0;
    });
    // 90:DB: the knob right-clicked since the last call (0 for none); clears the marks
    add(r, 0x90DB, "knob right-clicked", 0, 1, [s](Ctx& c) {
        Word found = 0;
        for (auto& [h, k] : s->knobs)
            if (k.flagged) found = h, k.flagged = false;
        c.push(found);
        return 0;
    });
    // 90:DC (knob, enabled)
    add(r, 0x90DC, "knob enable", 2, 0, [s](Ctx& c) {
        const Word on = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (Knob* k = knob_of(*s, c, h)) k->enabled = on != 0;
        return 0;
    });
    // 90:DD (mode) is the "swap display flag" of window_subops.cpp: the wheel mode the scroll below depends on.
    // 90:DE (knob): the wheel scrolls it from now on (newest first)
    add(r, 0x90DE, "knob wheel add", 1, 0, [s](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (knob_of(*s, c, h)) s->knob_wheel.insert(s->knob_wheel.begin(), h);
        return 0;
    });
    // 90:DF (knob)
    add(r, 0x90DF, "knob wheel remove", 1, 0, [s](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        auto it = std::find(s->knob_wheel.begin(), s->knob_wheel.end(), h);
        if (it == s->knob_wheel.end()) c.fail(Errc::Range, "the knob is not on the wheel list");
        else s->knob_wheel.erase(it);
        return 0;
    });
}

} // namespace obgi
