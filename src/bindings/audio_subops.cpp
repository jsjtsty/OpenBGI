#include "bindings/audio_subops.hpp"

#include <string>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

void check(Ctx& c, const Result<void>& r) {
    if (!r) c.fail(r.error().code, r.error().message);
}

// The wrappers validate before touching anything: channel in range, volume/pan at most 0x80.
bool channel_ok(Ctx& c, Bank b, Word ch) {
    if (ch >= Mixer::channel_count(b)) c.fail(Errc::Range, "sound channel " + std::to_string(ch) + " out of range");
    return c.ok();
}
bool level_ok(Ctx& c, Word v) {
    if (v > kMaxVolume) c.fail(Errc::Range, "sound volume or pan " + std::to_string(v) + " out of range");
    return c.ok();
}

Result<std::shared_ptr<Pcm>> load_sound(SoundHost& h, const std::string& prefix, const std::string& name) {
    auto bytes = h.resources->read(prefix, name);
    if (!bytes) return std::unexpected(bytes.error());
    return decode_bw(*bytes);
}

} // namespace

void register_audio_subops(vm::SubopRegistry& r, SoundHost& host) {
    Mixer& mx = *host.mixer;
    SoundHost* h = &host;

    // ---- configured (options) volume ----
    add(r, 0xA008, "set stream config volume", 2, 0, [&mx](Ctx& c) {
        Word vol = c.pop(), ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Stream, ch) && level_ok(c, vol)) check(c, mx.set_config_volume(Bank::Stream, ch, vol));
        return 0;
    });
    add(r, 0xA009, "set point config volume", 2, 0, [&mx](Ctx& c) {
        Word vol = c.pop(), ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Point, ch) && level_ok(c, vol)) check(c, mx.set_config_volume(Bank::Point, ch, vol));
        return 0;
    });

    // ---- streams (bank A) ----
    // (ch, prefix, name, volume, pan): loads the stream; it starts playing unless the script paused the channel first.
    add(r, 0xA011, "load stream", 5, 0, [&mx, h](Ctx& c) {
        Word pan = c.pop(), vol = c.pop();
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Stream, ch) || !level_ok(c, vol) || !level_ok(c, pan)) return 0;
        auto pcm = load_sound(*h, prefix, name);
        if (!pcm) {
            c.fail(pcm.error().code, pcm.error().message);
            return 0;
        }
        const bool loop = (*pcm)->loop;
        const std::uint32_t loop_start = (*pcm)->loop_start;
        check(c, mx.load(Bank::Stream, ch, *pcm, loop, loop_start));
        if (!c.ok()) return 0;
        check(c, mx.set_volume(Bank::Stream, ch, vol));
        check(c, mx.set_pan(Bank::Stream, ch, pan));
        if (mx.state(Bank::Stream, ch) != ChannelState::Paused) check(c, mx.start(Bank::Stream, ch));
        return 0;
    });
    // (ch, prefix, intro, loop file, loop switch, volume, pan): a single file loops from 0 when the switch is
    // set; two different files play the intro once, then repeat the second.
    add(r, 0xA012, "load stream (loop control)", 7, 0, [&mx, h](Ctx& c) {
        Word pan = c.pop(), vol = c.pop(), loop_sw = c.pop();
        std::string second = c.cstring(c.pop());
        std::string first = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Stream, ch) || !level_ok(c, vol) || !level_ok(c, pan)) return 0;
        auto a = load_sound(*h, prefix, first);
        if (!a) {
            c.fail(a.error().code, a.error().message);
            return 0;
        }
        std::shared_ptr<const Pcm> pcm = *a;
        bool loop = loop_sw != 0;
        std::uint32_t loop_start = 0;
        if (first != second) {
            auto b = load_sound(*h, prefix, second);
            if (!b) {
                c.fail(b.error().code, b.error().message);
                return 0;
            }
            auto joined = join_intro_loop(**a, **b);
            if (!joined) {
                c.fail(joined.error().code, joined.error().message);
                return 0;
            }
            loop_start = (*joined)->loop_start;
            pcm = *joined;
            loop = true; // the second file always repeats
        }
        check(c, mx.load(Bank::Stream, ch, pcm, loop, loop_start));
        if (!c.ok()) return 0;
        check(c, mx.set_volume(Bank::Stream, ch, vol));
        check(c, mx.set_pan(Bank::Stream, ch, pan));
        if (mx.state(Bank::Stream, ch) != ChannelState::Paused) check(c, mx.start(Bank::Stream, ch));
        return 0;
    });
    // (ch, switch): 0 pauses (also before the sound is loaded), anything else plays/resumes.
    add(r, 0xA014, "pause or play stream", 2, 0, [&mx](Ctx& c) {
        Word on = c.pop(), ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Stream, ch)) return 0;
        check(c, mx.set_paused(Bank::Stream, ch, on == 0));
        if (c.ok() && on != 0 && mx.state(Bank::Stream, ch) == ChannelState::Stopped) check(c, mx.start(Bank::Stream, ch));
        return 0;
    });
    add(r, 0xA016, "fade stream", 3, 0, [&mx](Ctx& c) {
        Word ms = c.pop(), target = c.pop(), ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Stream, ch) && level_ok(c, target)) check(c, mx.fade_to(Bank::Stream, ch, target, ms, false));
        return 0;
    });
    add(r, 0xA017, "set stream pan", 2, 0, [&mx](Ctx& c) {
        Word pan = c.pop(), ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Stream, ch) && level_ok(c, pan)) check(c, mx.set_pan(Bank::Stream, ch, pan));
        return 0;
    });
    add(r, 0xA018, "fade stream in", 2, 0, [&mx](Ctx& c) {
        Word ms = c.pop(), ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Stream, ch)) check(c, mx.fade_to(Bank::Stream, ch, kMaxVolume, ms, false));
        return 0;
    });
    add(r, 0xA019, "fade stream out", 2, 0, [&mx](Ctx& c) {
        Word ms = c.pop(), ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Stream, ch)) check(c, mx.fade_to(Bank::Stream, ch, 0, ms, false));
        return 0;
    });

    // ---- point sounds (bank B) ----
    add(r, 0xA020, "load point sound", 3, 0, [&mx, h](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Point, ch)) return 0;
        auto pcm = load_sound(*h, prefix, name);
        if (!pcm) {
            c.fail(pcm.error().code, pcm.error().message);
            return 0;
        }
        check(c, mx.load(Bank::Point, ch, *pcm, (*pcm)->loop, (*pcm)->loop_start));
        return 0;
    });
    // A0:21: the same load with a time in milliseconds and a value (their uses are not known yet)
    add(r, 0xA021, "load point sound (timed)", 5, 0, [&mx, h](Ctx& c) {
        c.pop(), c.pop();
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Point, ch)) return 0;
        auto pcm = load_sound(*h, prefix, name);
        if (!pcm) {
            c.fail(pcm.error().code, pcm.error().message);
            return 0;
        }
        check(c, mx.load(Bank::Point, ch, *pcm, (*pcm)->loop, (*pcm)->loop_start));
        return 0;
    });
    add(r, 0xA022, "free point sound", 1, 0, [&mx](Ctx& c) {
        Word ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Point, ch)) check(c, mx.unload(Bank::Point, ch));
        return 0;
    });
    // (ch, volume, pan): starts the sound; pushes 1 while it plays, 0 otherwise
    add(r, 0xA024, "play point sound", 3, 1, [&mx](Ctx& c) {
        Word pan = c.pop(), vol = c.pop(), ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Point, ch) || !level_ok(c, vol) || !level_ok(c, pan)) return 0;
        const bool ok = mx.play(Bank::Point, ch, vol, pan).has_value();
        c.push(ok && mx.state(Bank::Point, ch) == ChannelState::Playing ? 1 : 0);
        return 0;
    });
    add(r, 0xA025, "stop point sound", 1, 0, [&mx](Ctx& c) {
        Word ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Point, ch)) check(c, mx.stop(Bank::Point, ch));
        return 0;
    });
    add(r, 0xA026, "fade point sound out", 2, 0, [&mx](Ctx& c) {
        Word ms = c.pop(), ch = c.pop();
        if (c.ok() && channel_ok(c, Bank::Point, ch)) check(c, mx.fade_to(Bank::Point, ch, 0, ms, false));
        return 0;
    });
}

} // namespace obgi
