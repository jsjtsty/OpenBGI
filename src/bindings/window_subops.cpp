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
    // (virtual key): whether the key/button is currently held (GetAsyncKeyState bit 15 in the original).
    add(r, 0x8011, "input held", 1, 1, [s](Ctx& c) {
        const Word vk = c.pop();
        if (!c.ok()) return 0;
        if (vk >= s->input_held.size()) {
            c.fail(Errc::Range, "virtual key out of range: " + std::to_string(vk));
            return 0;
        }
        c.push(s->input_held[vk] ? 1u : 0u);
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
    // 80:1A (priority): consumes pending triggers through the key and mouse priority layers.
    add(r, 0x801A, "poll input", 1, 1, [s](Ctx& c) {
        const Word prio = c.pop();
        if (!c.ok()) return 0;
        c.push(poll_input(*s, prio));
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
    add(r, 0x8060, "window mode", 3, 0, [s, display](Ctx& c) {
        const Word a = c.pop(), b = c.pop(), cc = c.pop();
        if (!c.ok()) return 0;
        if (b > 1 || cc > 0xF) {
            c.fail(Errc::Range, "window mode arguments out of range");
            return 0;
        }
        if (display) {
            const auto [width, height] = s->pair_table[cc];
            if (width && height) {
                if (auto result = display->resize_screen(width, height); !result) {
                    c.fail(result.error().code, result.error().message);
                    return 0;
                }
            }
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
    // B0:81 (text, defaultYes) -> yes, B0:82 (text, style, defaultYes) -> confirmed: a question box; style 1 asks OK / cancel, anything else
    // yes / no. Without a host that answers, the default button does.
    const auto ask = [s](const std::string& text, bool ok_cancel, bool default_yes) {
        return s->confirm_box ? s->confirm_box(text, ok_cancel, default_yes) : default_yes;
    };
    add(r, 0xB081, "yes or no box", 2, 1, [ask](Ctx& c) {
        const Word default_yes = c.pop();
        const std::string text = c.cstring(c.pop());
        if (c.ok()) c.push(ask(text, false, default_yes != 0) ? 1 : 0);
        return 0;
    });
    add(r, 0xB082, "confirm box", 3, 1, [ask](Ctx& c) {
        const Word default_yes = c.pop(), style = c.pop();
        const std::string text = c.cstring(c.pop());
        if (c.ok()) c.push(ask(text, style == 1, default_yes != 0) ? 1 : 0);
        return 0;
    });
    // B0:8C (out, size, caption, items): the language chooser of other builds; the reference has no dialog either, drops the operands and
    // answers 0 (cancelled)
    add(r, 0xB08C, "choose from list box", 4, 1, [](Ctx& c) {
        for (int i = 0; i < 4; ++i) c.pop();
        c.push(0);
        return 0;
    });
    // B0:08 (pattern, amplitude, frequency, repeat, decay, fps, skip): the screen quake (wait_quake.c). The thread waits until it ends; while
    // it runs the whole screen is presented shifted by a triangle wave of the amplitude at `frequency` cycles a second, sampled `fps` times a
    // second; pattern 0 shakes horizontally, 1 vertically, 2 diagonally, 3 along a random direction that changes every cycle. The amplitude
    // loses `decay` percent per cycle, `repeat` cycles end it, as does any input when `skip` is set.
    auto quake_rand = std::make_shared<std::uint32_t>(0x1234567u);
    add(r, 0xB008, "screen quake", 7, 0, [s, display, quake_rand](Ctx& c) {
        const auto skip = static_cast<std::int32_t>(c.pop()), fps = static_cast<std::int32_t>(c.pop()), decay = static_cast<std::int32_t>(c.pop());
        const auto repeat = static_cast<std::int32_t>(c.pop()), frequency = static_cast<std::int32_t>(c.pop()), amplitude = static_cast<std::int32_t>(c.pop());
        const auto pattern = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (static_cast<std::uint32_t>(pattern) >= 4) { c.fail(Errc::Range, "invalid shake pattern " + std::to_string(pattern)); return 0; }
        if (frequency < 1) { c.fail(Errc::Range, "invalid shake frequency " + std::to_string(frequency)); return 0; }
        if (repeat < 1) { c.fail(Errc::Range, "invalid shake repeat count " + std::to_string(repeat)); return 0; }
        if (fps < 1 || fps < frequency) { c.fail(Errc::Range, "invalid frame rate " + std::to_string(fps)); return 0; }
        if (!display) return 0;
        // the per-frame offsets, simulated as the reference's wait does (a random value is the mean of eight 30-bit draws)
        static constexpr bool kWaveX[4] = {true, false, true, false}, kRandom[4] = {false, false, false, true}, kWaveY[4] = {false, true, true, false};
        const auto random_to = [quake_rand](std::int32_t n) {
            std::int32_t sum = 0;
            const auto draw = [quake_rand] { *quake_rand = *quake_rand * 214013u + 2531011u; return static_cast<std::int32_t>((*quake_rand >> 16) & 0x7FFF); };
            for (int i = 0; i < 8; ++i) sum += ((draw() << 15) | draw()) % (n + 1);
            return sum >> 3;
        };
        auto offsets = std::make_shared<std::vector<std::pair<std::int32_t, std::int32_t>>>();
        const std::int32_t frames_per_cycle = fps / frequency;
        std::int32_t amp = amplitude << 11, phase = 0, step = 0;
        std::int32_t dir_start[2] = {0, 0}, dir_end[2] = {0, 0}, dir_delta[2] = {0, 0};
        for (std::int32_t cycle = 0; cycle < repeat && offsets->size() < 0x100000; ++cycle) {
            for (std::int32_t frame = 0; frame < frames_per_cycle; ++frame) {
                if (frame == 0) {
                    step = (amp * 4) / frames_per_cycle, phase = 0;
                    dir_start[0] = dir_end[0], dir_start[1] = dir_end[1];
                    if (pattern == 3) {
                        if (amp <= 0) dir_end[0] = dir_end[1] = 0;
                        else {
                            const std::int32_t rx = random_to(amp), ry = random_to(amp), quadrant = cycle & 3;
                            dir_end[0] = quadrant == 1 || quadrant == 3 ? -rx : rx;
                            dir_end[1] = quadrant == 2 || quadrant == 3 ? -ry : ry;
                        }
                    }
                    dir_delta[0] = dir_end[0] - dir_start[0], dir_delta[1] = dir_end[1] - dir_start[1];
                }
                phase += step;
                if (phase > amp || phase < -amp) step = -step;
                if (phase > amp) phase = 2 * amp - phase;
                if (phase < -amp) phase = -phase - 2 * amp;
                std::int32_t x = kWaveX[pattern] ? phase : 0, y = kWaveY[pattern] ? phase : 0;
                if (kRandom[pattern]) {
                    x += dir_delta[0] * frame / frames_per_cycle + dir_start[0];
                    y += dir_delta[1] * frame / frames_per_cycle + dir_start[1];
                }
                offsets->emplace_back(x >> 12, y >> 12);
            }
            amp = (100 - decay) * amp / 100;
        }
        const std::int64_t start = c.vm.now_ms;
        const std::int64_t frame_ms = std::max<std::int64_t>(1, 1000 / fps);
        const auto input_total = [s] { std::uint32_t sum = 0; for (std::uint32_t n : s->input_counts) sum += n; return sum; };
        const std::uint32_t input_at_start = input_total();
        vm::WaitObject wait;
        wait.kind = vm::WaitObject::Kind::Poll;
        wait.cleanup = [display] { display->present_dx = display->present_dy = 0; };
        wait.poll = [display, offsets, start, frame_ms, skip, input_total, input_at_start](const vm::WaitContext& context) -> Result<vm::WaitUpdate> {
            const std::int64_t frame = (context.now_ms - start) / frame_ms;
            if (frame >= static_cast<std::int64_t>(offsets->size()) || (skip && input_total() != input_at_start)) {
                display->present_dx = display->present_dy = 0; // the window is repainted unshifted when it ends
                return vm::WaitUpdate::complete();
            }
            display->present_dx = (*offsets)[static_cast<std::size_t>(frame)].first;
            display->present_dy = (*offsets)[static_cast<std::size_t>(frame)].second;
            return vm::WaitUpdate::pending();
        };
        c.suspend(std::move(wait), 0);
        return static_cast<int>(vm::StayThread);
    });
    add_record(r, state, 0x8164, "client size", 2);
    add(r, 0x8167, "get client size", 1, 0, [s, display](Ctx& c) {
        const Word dst = c.pop();
        auto w = display ? display->screen_width() : s->display_width;
        auto h = display ? display->screen_height() : s->display_height;
        const auto it = s->records.find(0x8164);
        if (it != s->records.end() && it->second.size() == 2 &&
            static_cast<std::int32_t>(it->second[0]) > 0 && static_cast<std::int32_t>(it->second[1]) > 0) {
            w = it->second[0]; h = it->second[1];
        }
        c.write(dst, w, 4); c.write(dst + 4, h, 4);
        return 0;
    });
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
    // Records kept per item (a history list; the original stores them compressed, which a script cannot see).
    // 80:9C (item, address): pushes a copy of b bytes at the address on the front and drops the oldest beyond a; 80:9A
    // (item, out address): writes how many are kept; 80:9D (out address, item, index): copies a record out (2 when there is
    // none at that index); 80:9E (item, ?, count): drops the count newest. All push 0, or 1 when the item does not exist.
    const auto find_item = [s](Word id) {
        return std::find_if(s->items.begin(), s->items.end(), [&](const auto& x) { return x.id == id; });
    };
    // 80:A8 (item, value): the original sets a property of the object behind the item (the engine's 0xC1 value); pushes 1 when the item exists
    add(r, 0x80A8, "set item value", 2, 1, [s, find_item](Ctx& c) {
        const Word value = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        auto it = find_item(id);
        if (it != s->items.end()) it->value = value;
        c.push(it != s->items.end() ? 1 : 0);
        return 0;
    });
    add(r, 0x809C, "push item record", 2, 1, [s, find_item](Ctx& c) {
        const Word addr = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        auto it = find_item(id);
        if (it == s->items.end()) {
            c.push(1);
            return 0;
        }
        std::vector<std::uint8_t> rec;
        c.read_bytes(addr, it->b, rec);
        if (!c.ok()) return 0;
        it->entries.push_front(std::move(rec));
        while (it->entries.size() > it->a) it->entries.pop_back();
        c.push(0);
        return 0;
    });
    add(r, 0x809A, "item record count", 2, 1, [s, find_item](Ctx& c) {
        const Word id = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        auto it = find_item(id);
        if (it != s->items.end()) c.write(out, static_cast<Word>(it->entries.size()), 4);
        c.push(it == s->items.end());
        return 0;
    });
    add(r, 0x809D, "read item record", 3, 1, [s, find_item](Ctx& c) {
        const Word index = c.pop(), id = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        auto it = find_item(id);
        if (it == s->items.end()) c.push(1);
        else if (index >= it->entries.size()) c.push(2);
        else {
            c.write_bytes(out, it->entries[index].data(), it->entries[index].size());
            c.push(0);
        }
        return 0;
    });
    add(r, 0x809E, "drop item records", 3, 1, [s, find_item](Ctx& c) {
        const Word count = c.pop();
        c.pop();
        const Word id = c.pop();
        if (!c.ok()) return 0;
        auto it = find_item(id);
        if (it == s->items.end()) c.push(1);
        else if (it->entries.empty()) c.push(2);
        else {
            for (Word k = 0; k < count && !it->entries.empty(); ++k) it->entries.pop_front();
            c.push(0);
        }
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
        const Word handle = 0x90000000u + static_cast<Word>(free_slot - s->filter_used.begin());
        if (s->display) {
            Sprite& sp = s->display->sprite(handle);
            sp = Sprite{};
            sp.visible = false, sp.filter_kind = 0, sp.alpha = 0; // hidden until 90:64
        }
        c.push(handle);
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
        if (s->display) s->display->remove_sprite(h);
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
        if (s->display) s->display->set_visible(h, value != 0); // 90:64 (filter, show)
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
        // The game's 0x7fff system message asks the message thread to save and terminate.
        if (m[0] == 0 && m[1] == 0x7fff && m[2] == 0) s->shutdown_message_thread = c.t.id;
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
    // B0:06: whether the system cursor is shown (the original asks GetCursorInfo when its own flag is unset); always shown here
    add(r, 0xB006, "cursor shown", 0, 1, [](Ctx& c) {
        c.push(1);
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
    add_record(r, state, 0x9091, "text input layer mode", 2);
    add_record(r, state, 0x91BF, "user key map", 2);
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
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:0C shown=%u level=%u\n", q, p);
        if (s->display) s->display->windows_shown = q != 0, s->display->windows_level = p;
        return 0;
    });
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
    // 90:65 (filter, colour, level 0..0x100, priority) is a uniform kind 0 filter: what is drawn below its priority is blended
    // towards the colour by level / 256
    add(r, 0x9065, "filter set", 4, 0, [s](Ctx& c) {
        const Word prio = c.pop(), level = c.pop(), colour = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (prio > 0xFFFF || level > 0x100) {
            c.fail(Errc::Range, "filter priority or level out of range");
            return 0;
        }
        Sprite* sp = s->display ? s->display->find_sprite(h) : nullptr;
        if (!sp || sp->filter_kind < 0) {
            c.fail(Errc::Range, "not a filter object handle: " + std::to_string(h));
            return 0;
        }
        sp->filter_kind = 0, sp->filter_colour = colour, sp->alpha = level, sp->priority = prio;
        sp->filter_gray = 0xFFFFFFFFu; // filter.c: Filter_Set drops the gray bitmap
        return 0;
    });
    // 90:66 (filter, kind 0..3, colour, gray bitmap, param, level, priority): kind 0 blends toward the colour, 1 adds it, 2 tints by
    // luminance, 3 blends toward the picture XOR the colour; a screen-sized gray bitmap (-1 = none) modulates the tint per pixel
    // with `param` as its transition curve. The state is set first and then the bitmap checked: no such bitmap, not gray, not screen sized.
    add(r, 0x9066, "filter set (kind)", 7, 0, [s](Ctx& c) {
        const Word prio = c.pop(), level = c.pop();
        const auto param = static_cast<std::int32_t>(c.pop()), bmp = static_cast<std::int32_t>(c.pop());
        const Word colour = c.pop(), kind = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        Sprite* sp = s->display ? s->display->find_sprite(h) : nullptr;
        if (!sp || sp->filter_kind < 0) {
            c.fail(Errc::Range, "not a filter object handle: " + std::to_string(h));
            return 0;
        }
        s->records[0x9066] = {h, kind, colour, static_cast<Word>(bmp), static_cast<Word>(param), level, prio};
        sp->filter_kind = static_cast<int>(std::min<Word>(kind, 4)); // a kind above 3 draws nothing
        sp->filter_colour = colour, sp->alpha = level, sp->priority = prio;
        sp->filter_gray = 0xFFFFFFFFu;
        if (bmp == -1) return 0;
        const SlotInfo* info = bmp >= 0 && bmp < 0x4000 ? s->display->slot(static_cast<Word>(bmp)) : nullptr;
        if (!info) { c.fail(Errc::Range, "no image slot " + std::to_string(bmp)); return 0; }
        if (info->format != SlotFormat::Gray8) { c.fail(Errc::Range, "bitmap " + std::to_string(bmp) + " is not a gray bitmap"); return 0; }
        if (info->width != s->display->screen_width() || info->height != s->display->screen_height()) { c.fail(Errc::Range, "bitmap " + std::to_string(bmp) + " is not screen sized"); return 0; }
        sp->filter_gray = static_cast<Word>(bmp), sp->filter_param = param;
        return 0;
    });
    // 90:09 (priority): objects below this priority are not drawn (the background layers are)
    add(r, 0x9009, "draw limit", 1, 0, [s](Ctx& c) {
        const Word prio = c.pop();
        if (!c.ok()) return 0;
        if (prio > 0xFFFF) {
            c.fail(Errc::Range, "priority " + std::to_string(prio) + " is not allowed");
            return 0;
        }
        if (s->display) s->display->draw_limit = prio;
        return 0;
    });
}

} // namespace obgi
