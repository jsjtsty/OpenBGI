// Window, display-mode, input-map and typesetting settings the scripts make at start-up. Most of these only hand
// numbers to a subsystem (window, input, text) that is not built yet, so they are recorded in SystemState::records.
#include <algorithm>

#include "bindings/system_subops.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

// pop `n` values and keep them (in push order) under the op's id
void add_record(vm::SubopRegistry& r, SystemState& s, std::uint16_t id, const char* name, int n) {
    SystemState* st = &s;
    add(r, id, name, n, 0, [st, id, n](Ctx& c) {
        std::vector<Word> args(static_cast<std::size_t>(n));
        for (int i = n - 1; i >= 0; --i) args[std::size_t(i)] = c.pop();
        if (c.ok()) st->records[id] = std::move(args);
        return 0;
    });
}

// pop two values (a first, then b); b must be 1..0x100; kept as {b, a}
void add_range_pair(vm::SubopRegistry& r, SystemState& s, std::uint16_t id, const char* name) {
    SystemState* st = &s;
    add(r, id, name, 2, 0, [st, id, name](Ctx& c) {
        const Word a = c.pop(), b = c.pop();
        if (!c.ok()) return 0;
        if (b < 1 || b > 0x100) {
            c.fail(Errc::Range, std::string(name) + ": " + std::to_string(b) + " is not in 1..256");
            return 0;
        }
        st->records[id] = {b, a};
        return 0;
    });
}

} // namespace

std::vector<std::uint32_t> SystemState::input_numbers(std::uint32_t bit) const {
    if (auto it = input_maps.find(bit); it != input_maps.end()) return it->second;
    switch (bit) { // the original's initial table
    case 0x1: return {1};
    case 0x2: return {2};
    case 0x4: return {4};
    case 0x10: return {5};
    case 0x20: return {6};
    case 0x40: return {0x0E};
    case 0x80: return {0x0F};
    case 0x100: return {0x0D};
    case 0x200: return {0x20};
    case 0x1000: return {0x26};
    case 0x2000: return {0x28};
    case 0x4000: return {0x25};
    case 0x8000: return {0x27};
    case 0x2000000: return {0x30, 0x60};
    case 0x40000000: return {0x09};
    case 0x80000000: return {0x11};
    default:
        if (bit >= 0x10000 && bit <= 0x1000000) { // digits 1..9
            std::uint32_t k = 0;
            while ((0x10000u << k) != bit) ++k;
            return {0x31 + k, 0x61 + k};
        }
        return {};
    }
}

namespace {

// a zero-terminated list of at most 15 numbers at a script address (0 = no list)
bool read_number_list(Ctx& c, Word addr, std::vector<Word>& out, const char* what) {
    out.clear();
    if (addr == 0) return true;
    for (;;) {
        const Word v = static_cast<Word>(c.read(addr + 4 * static_cast<Word>(out.size()), 4, false));
        if (!c.ok()) return false;
        if (v == 0) return true;
        out.push_back(v);
        if (out.size() > 15) {
            c.fail(Errc::Range, std::string(what) + ": more than 15 numbers");
            return false;
        }
    }
}

// pop one value; below `limit` it is kept and 1 is pushed, otherwise 0 is pushed
void add_limited_flag(vm::SubopRegistry& r, SystemState& s, std::uint16_t id, const char* name, Word limit) {
    SystemState* st = &s;
    add(r, id, name, 1, 1, [st, id, limit](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        if (v < limit) st->values[id] = v;
        c.push(v < limit ? 1 : 0);
        return 0;
    });
}

} // namespace

void register_window_subops(vm::SubopRegistry& r, SystemState& state, Display* display) {
    SystemState* s = &state;

    // (destination): writes the desktop width and height (u32 each); a very wide desktop counts as one screen
    add(r, 0x810E, "display size", 1, 0, [s](Ctx& c) {
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        std::uint32_t w = s->display_width, h = s->display_height;
        if (w != 0 && h != 0) {
            if (w * 4 / h > 9) w /= 2;
            if (w != 0 && h / w != 0 && h * 100 / w < 125) h /= 2;
        }
        c.write(dst, w, 4);
        c.write(dst + 4, h, 4);
        return 0;
    });

    // (mask, numbers): the input numbers (keys, buttons) that make up one input group
    add(r, 0x801B, "input map", 2, 0, [s](Ctx& c) {
        const Word addr = c.pop(), mask = c.pop();
        std::vector<Word> list;
        if (!c.ok() || !read_number_list(c, addr, list, "input map")) return 0;
        static constexpr Word kMasks[] = {0x40, 0x80, 0x100, 0x200, 0x1000, 0x2000, 0x4000, 0x8000, 0x40000000, 0x80000000};
        if (std::find(std::begin(kMasks), std::end(kMasks), mask) == std::end(kMasks)) {
            c.fail(Errc::Range, "input mask " + std::to_string(mask) + " is not one of the input groups");
            return 0;
        }
        s->input_maps[mask] = std::move(list);
        return 0;
    });
    // (list address): the sum of the press counts of the zero-terminated list of input numbers
    add(r, 0x8012, "input state sum by list", 1, 1, [s](Ctx& c) {
        std::vector<Word> list;
        const Word addr = c.pop();
        if (!c.ok() || !read_number_list(c, addr, list, "input list")) return 0;
        Word sum = 0;
        for (Word n : list) sum += n < s->input_counts.size() ? s->input_counts[n] : 0;
        c.push(sum);
        return 0;
    });
    // (mask): the same sum over every input number behind the set bits of the logical input mask
    add(r, 0x801C, "input state sum by mask", 1, 1, [s](Ctx& c) {
        const Word mask = c.pop();
        if (!c.ok()) return 0;
        Word sum = 0;
        for (Word bit = 1; bit != 0; bit <<= 1)
            if (mask & bit)
                for (Word n : s->input_numbers(bit)) sum += n < s->input_counts.size() ? s->input_counts[n] : 0;
        c.push(sum);
        return 0;
    });
    add(r, 0x8062, "window numbers", 2, 0, [s](Ctx& c) {
        const Word addr = c.pop(), id = c.pop();
        std::vector<Word> list;
        if (!c.ok() || !read_number_list(c, addr, list, "window numbers")) return 0;
        s->window_id = id;
        if (id != 0) s->window_numbers = std::move(list);
        return 0;
    });
    // (name, numbers) -> handle: a named group of archives
    add(r, 0x8038, "archive group", 2, 1, [s](Ctx& c) {
        const Word addr = c.pop();
        std::string name = c.cstring(c.pop());
        std::vector<Word> list;
        if (!c.ok() || !read_number_list(c, addr, list, "archive group")) return 0;
        s->archive_groups.push_back({std::move(name), std::move(list)});
        c.push(1); // the real game answers 1 for every group
        return 0;
    });
    // (a, b, c): window / display mode; b is 0 or 1 and c below 16
    add(r, 0x8060, "window mode", 3, 0, [s](Ctx& c) {
        const Word a = c.pop(), b = c.pop(), cc = c.pop();
        if (!c.ok()) return 0;
        if (b > 1 || cc > 0xF) {
            c.fail(Errc::Range, "window mode arguments out of range");
            return 0;
        }
        s->records[0x8060] = {cc, b, a};
        return 0;
    });
    // (message): a message box on the main window
    add(r, 0xB080, "message box", 1, 0, [s](Ctx& c) {
        std::string text = c.cstring(c.pop());
        if (c.ok() && s->message_box) s->message_box(text);
        return 0;
    });
    add_record(r, state, 0x8164, "client size", 2);
    add_record(r, state, 0x8064, "show window", 1);
    // the window moves back to its default place; 1 = done
    add(r, 0xB002, "reset window position", 0, 1, [](Ctx& c) {
        c.push(1);
        return 0;
    });
    // (title): the window title (kept up to 255 bytes)
    add(r, 0x8066, "window title", 1, 0, [s](Ctx& c) {
        std::string title = c.cstring(c.pop());
        if (!c.ok()) return 0;
        if (title.size() > 255) title.resize(255);
        s->window_title = std::move(title);
        return 0;
    });
    // (new value) -> old value: swaps a global the display code keeps (initially 1)
    add(r, 0x90DD, "swap display flag", 1, 1, [s](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        auto [it, fresh] = s->values.try_emplace(0x90DD, 1u);
        c.push(it->second);
        it->second = v;
        return 0;
    });
    // (out id address, a, b): registers an item; a and b must both be non-zero (status 2 otherwise). The new
    // number is written to the address, the status (0) is pushed.
    add(r, 0x8098, "register item", 3, 1, [s](Ctx& c) {
        const Word b = c.pop(), a = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        if (a == 0 || b == 0) {
            c.push(2);
            return 0;
        }
        const Word id = ++s->last_item_id;
        s->items.insert(s->items.begin(), {id, a, b, 1});
        if (out != 0) c.write(out, id, 4);
        c.push(0);
        return 0;
    });
    // 90:60: creates a filter display object (at most 8); pushes its handle, 0x90000000 + slot
    add(r, 0x9060, "create filter object", 0, 1, [s](Ctx& c) {
        auto free_slot = std::find(s->filter_used.begin(), s->filter_used.end(), false);
        if (free_slot == s->filter_used.end()) {
            c.fail(Errc::Range, "too many filter objects (the limit is 8)");
            return 0;
        }
        *free_slot = true;
        c.push(0x90000000u + static_cast<Word>(free_slot - s->filter_used.begin()));
        return 0;
    });
    // (handle): deletes a filter object; an unknown handle is a script error
    add(r, 0x9061, "delete filter object", 1, 0, [s](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        const Word slot = h - 0x90000000u;
        if (slot >= s->filter_used.size() || !s->filter_used[slot]) {
            c.fail(Errc::Range, "not a filter object handle: " + std::to_string(h));
            return 0;
        }
        s->filter_used[slot] = false;
        return 0;
    });
    // (handle, value): a filter parameter. The filter effects themselves are not drawn yet.
    add(r, 0x9064, "set filter parameter", 2, 0, [s](Ctx& c) {
        const Word value = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        const Word slot = h - 0x90000000u;
        if (slot >= s->filter_used.size() || !s->filter_used[slot]) {
            c.fail(Errc::Range, "not a filter object handle: " + std::to_string(h));
            return 0;
        }
        s->records[0x9064] = {h, value};
        return 0;
    });
    // 80:A1 (a, b): appends the message {0, b, a} to the global queue
    add(r, 0x80A1, "queue message", 2, 0, [s](Ctx& c) {
        const Word a = c.pop(), b = c.pop();
        if (c.ok()) s->event_queue.push_back({0, b, a});
        return 0;
    });
    // 80:A0 (destination): takes the oldest message off the queue and writes its three words there; pushes 1, or 0 when
    // the queue is empty
    add(r, 0x80A0, "take message", 1, 1, [s](Ctx& c) {
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        if (s->event_queue.empty()) {
            c.push(0);
            return 0;
        }
        const auto m = s->event_queue.front();
        s->event_queue.pop_front();
        if (dst != 0)
            for (int i = 0; i < 3; ++i) c.write(dst + 4u * i, m[std::size_t(i)], 4);
        c.push(1);
        return 0;
    });
    // (x, y): moves the window; 1 = the position was acceptable (the engine's window is the host's business)
    add(r, 0xB003, "move window", 2, 1, [s](Ctx& c) {
        const Word y = c.pop(), x = c.pop();
        if (!c.ok()) return 0;
        s->records[0xB003] = {x, y};
        c.push(1);
        return 0;
    });
    // (milliseconds): starts the timed transition, or stops and resets it when one is already running
    add(r, 0xB005, "toggle transition", 1, 0, [s](Ctx& c) {
        const Word ms = c.pop();
        if (!c.ok()) return 0;
        if (!s->transition_running) {
            s->transition_running = true;
            s->transition_end_ms = c.vm.now_ms + ms;
        } else {
            s->transition_running = false;
            s->transition_end_ms = 0;
        }
        return 0;
    });
    // fullscreen / rendering mode requests: 0, 2, 3 and 1, 4 are accepted, anything else answers 0
    add(r, 0x816F, "screen mode", 1, 1, [s](Ctx& c) {
        const Word mode = c.pop();
        if (!c.ok()) return 0;
        const bool ok = mode <= 4;
        if (ok) s->values[0x816F] = mode;
        c.push(ok ? 1 : 0);
        return 0;
    });
    add_limited_flag(r, state, 0x8163, "setting 81:63", 3);

    // ---- typesetting and display settings (analysis/排版引擎.md) ----
    add_record(r, state, 0x9094, "setting 90:94", 1);
    add_range_pair(r, state, 0x9095, "setting 90:95");
    add_range_pair(r, state, 0x9096, "setting 90:96");
    add_record(r, state, 0x9097, "setting 90:97", 2);
    add_record(r, state, 0x909C, "setting 90:9C", 1);
    add_record(r, state, 0x9198, "text spacing", 6);
    add_record(r, state, 0x9297, "text ruby layout", 7);
    add(r, 0x900C, "display setting 90:0C", 2, 0, [s](Ctx& c) {
        const Word p = c.pop(), q = c.pop();
        if (!c.ok()) return 0;
        if (p > 0x100) {
            c.fail(Errc::Range, "display setting 90:0C: " + std::to_string(p) + " is above 256");
            return 0;
        }
        s->records[0x900C] = {q, p};
        return 0;
    });
    add_record(r, state, 0x904C, "display setting 90:4C", 2);
    add_record(r, state, 0x9000, "display update flag", 1);
    add_record(r, state, 0x9007, "display setting 90:07", 1);
    add_record(r, state, 0x909B, "typesetting setting 90:9B", 2);
    add_record(r, state, 0x9099, "global switch 90:99", 1);
    add_record(r, state, 0x909A, "setting 90:9A", 3);
    add_record(r, state, 0x909E, "setting 90:9E", 2);
    // (count, address of count image slot ids): the message character bitmaps; -1 entries are empty, every other one
    // has to exist. A count of 0 or 1 clears the list.
    add(r, 0x9098, "message bitmap list", 2, 0, [st = &state, display](Ctx& c) {
        const Word addr = c.pop(), count = c.pop();
        if (!c.ok()) return 0;
        std::vector<Word> ids;
        if (count > 1) {
            if (count > 0x1000) {
                c.fail(Errc::Range, "message bitmap list: " + std::to_string(count) + " entries");
                return 0;
            }
            for (Word i = 0; i < count; ++i) ids.push_back(static_cast<Word>(c.read(addr + i * 4, 4, false)));
            if (!c.ok()) return 0;
            for (Word id : ids)
                if (id != 0xFFFFFFFFu && display && !display->slot(id)) {
                    c.fail(Errc::Range, "message bitmap list: no image slot " + std::to_string(id));
                    return 0;
                }
        }
        st->records[0x9098] = std::move(ids);
        return 0;
    });
    // (x, y, density 0..256): negative coordinates are errors
    add(r, 0x909D, "density at point", 3, 0, [st = &state](Ctx& c) {
        const Word density = c.pop(), y = c.pop(), x = c.pop();
        if (!c.ok()) return 0;
        if (static_cast<std::int32_t>(x) < 0 || static_cast<std::int32_t>(y) < 0 || density > 0x100) {
            c.fail(Errc::Range, "density at point: bad coordinates or density");
            return 0;
        }
        st->records[0x909D] = {x, y, density};
        return 0;
    });
    add_record(r, state, 0x9065, "filter setting 90:65", 4);
    add_record(r, state, 0x9066, "filter bitmap 90:66", 7);
    add_record(r, state, 0x9009, "display setting 90:09", 1); // the original also range-checks the value (limit unknown)
}

} // namespace obgi
