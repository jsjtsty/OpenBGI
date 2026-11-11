#include "bindings/audio_subops.hpp"

#include <string>
#include <chrono>
#include <functional>
#include "resources/decoded_cache.hpp"

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

using SoundCache = DecodedCache<Pcm>;
struct SoundLoad {
    SoundCache::Request first, second, joined;
    SoundCache::Pointer a, b;
    bool join = false;
    std::optional<Result<SoundCache::Pointer>> poll(SoundCache& cache) {
        if (!a) {
            auto result = cache.poll(first);
            if (result) {
                if (!*result) return std::unexpected(result->error());
                a = **result; first.reset();
            }
        }
        if (join && !b) {
            auto result = cache.poll(second);
            if (result) {
                if (!*result) return std::unexpected(result->error());
                b = **result; second.reset();
            }
        }
        if (!a || (join && !b)) return std::nullopt;
        if (!join) return a;
        if (!joined) {
            const auto bytes = (a->samples.size() + b->samples.size()) * sizeof(std::int16_t) * 2;
            joined = cache.transform("intro-loop", [a = a, b = b]() -> Result<SoundCache::Pointer> {
                auto pcm = join_intro_loop(*a, *b);
                if (!pcm) return std::unexpected(pcm.error());
                return *pcm;
            }, bytes);
        }
        return cache.poll(joined);
    }
};

// Complete on the VM thread, never from a decoder or the audio callback.
int load_sound(Ctx& c, const std::shared_ptr<SoundCache>& cache, Mixer& mixer, Bank bank, Word ch,
               std::string prefix, std::string first, std::string second, bool join,
               std::function<Result<void>(SoundCache::Pointer)> install) {
    const auto ticket = mixer.reserve_load(bank, ch);
    auto a = cache->request(prefix, first);
    if (!a) { c.fail(a.error().code, a.error().message); return 0; }
    auto operation = std::make_shared<SoundLoad>(); operation->first = std::move(*a); operation->join = join;
    if (join) {
        auto b = cache->request(prefix, second);
        if (!b) { c.fail(b.error().code, b.error().message); return 0; }
        operation->second = std::move(*b);
    }
    auto complete = [cache, operation, &mixer, bank, ch, ticket, install = std::move(install), prefix, first]() -> Result<vm::WaitUpdate> {
        if (mixer.load_ticket(bank, ch) != ticket) return vm::WaitUpdate::complete();
        auto result = operation->poll(*cache);
        if (!result) return vm::WaitUpdate::pending();
        if (!*result) return fail(result->error().code, "load sound [" + prefix + "/" + first + "]: " + result->error().message);
        const auto start = std::chrono::steady_clock::now();
        auto applied = install(**result);
        if (!applied) return std::unexpected(applied.error());
        if (std::getenv("OBGI_PERF_RESOURCE")) std::fprintf(stderr, "RESOURCE sound-commit [%s/%s] channel=%u us=%lld\n", prefix.c_str(), first.c_str(), ch,
            static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()));
        return vm::WaitUpdate::complete();
    };
    auto result = complete();
    if (!result) { c.fail(result.error().code, result.error().message); return 0; }
    if (result->status == vm::WaitUpdate::Status::Complete) return 0;
    vm::WaitObject wait; wait.kind = vm::WaitObject::Kind::Resource;
    wait.poll = [complete = std::move(complete)](const vm::WaitContext&) { return complete(); };
    c.suspend(std::move(wait), 0);
    return static_cast<int>(vm::StayThread);
}

} // namespace

void register_audio_subops(vm::SubopRegistry& r, SoundHost& host) {
    Mixer& mx = *host.mixer;
    auto cache = std::make_shared<SoundCache>(*host.resources, [](OwnedBytes raw) -> Result<SoundCache::Pointer> {
        auto bytes = unpack_resource(std::move(raw));
        if (!bytes) return std::unexpected(bytes.error());
        auto pcm = decode_bw(bytes->bytes);
        if (!pcm) return std::unexpected(pcm.error());
        return *pcm;
    }, [](const Pcm& pcm) { return pcm.samples.size() * sizeof(std::int16_t); }, 64u * 1024u * 1024u, host.jobs);

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
    add(r, 0xA011, "load stream", 5, 0, [&mx, cache](Ctx& c) {
        Word pan = c.pop(), vol = c.pop();
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Stream, ch) || !level_ok(c, vol) || !level_ok(c, pan)) return 0;
        return load_sound(c, cache, mx, Bank::Stream, ch, prefix, name, {}, false,
            [&mx, ch, vol, pan](SoundCache::Pointer pcm) -> Result<void> {
                if (auto r = mx.load(Bank::Stream, ch, pcm, pcm->loop, pcm->loop_start); !r) return r;
                if (auto r = mx.set_volume(Bank::Stream, ch, vol); !r) return r;
                if (auto r = mx.set_pan(Bank::Stream, ch, pan); !r) return r;
                return mx.state(Bank::Stream, ch) == ChannelState::Paused ? Result<void>{} : mx.start(Bank::Stream, ch);
            });
    });
    // (ch, prefix, intro, loop file, loop switch, volume, pan): a single file loops from 0 when the switch is
    // set; two different files play the intro once, then repeat the second.
    add(r, 0xA012, "load stream (loop control)", 7, 0, [&mx, cache](Ctx& c) {
        Word pan = c.pop(), vol = c.pop(), loop_sw = c.pop();
        std::string second = c.cstring(c.pop());
        std::string first = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Stream, ch) || !level_ok(c, vol) || !level_ok(c, pan)) return 0;
        const bool join = first != second;
        return load_sound(c, cache, mx, Bank::Stream, ch, prefix, first, second, join,
            [&mx, ch, vol, pan, join, loop = loop_sw != 0](SoundCache::Pointer pcm) -> Result<void> {
                if (auto r = mx.load(Bank::Stream, ch, pcm, join || loop, join ? pcm->loop_start : 0); !r) return r;
                if (auto r = mx.set_volume(Bank::Stream, ch, vol); !r) return r;
                if (auto r = mx.set_pan(Bank::Stream, ch, pan); !r) return r;
                return mx.state(Bank::Stream, ch) == ChannelState::Paused ? Result<void>{} : mx.start(Bank::Stream, ch);
            });
    });
    // (ch, switch): 0 pauses (also before the sound is loaded), anything else plays/resumes.
    add(r, 0xA014, "pause or play stream", 2, 0, [&mx](Ctx& c) {
        Word on = c.pop(), ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Stream, ch)) return 0;
        check(c, mx.set_paused(Bank::Stream, ch, on == 0));
        if (c.ok() && on != 0 && mx.state(Bank::Stream, ch) == ChannelState::Stopped) check(c, mx.start(Bank::Stream, ch));
        return 0;
    });
    // (ch, &loops): pushes 1 while the stream plays (0 otherwise, also for a bad channel); then the address receives the loop count. The
    // reference calls these "position" and "length" but its library answers the playing status and the loop counter (snd/sound.c)
    add(r, 0xA015, "stream playing and loop count", 2, 1, [&mx](Ctx& c) {
        const Word out = c.pop(), ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Stream, ch)) return 0;
        const bool playing = mx.state(Bank::Stream, ch) == ChannelState::Playing;
        if (playing) c.write(out, mx.loop_count(Bank::Stream, ch), 4);
        c.push(playing ? 1 : 0);
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
    add(r, 0xA020, "load point sound", 3, 0, [&mx, cache](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Point, ch)) return 0;
        return load_sound(c, cache, mx, Bank::Point, ch, prefix, name, {}, false,
            [&mx, ch](SoundCache::Pointer pcm) { return mx.load(Bank::Point, ch, pcm, pcm->loop, pcm->loop_start); });
    });
    // A0:21: the same load with a time in milliseconds and a value (their uses are not known yet)
    add(r, 0xA021, "load point sound (timed)", 5, 0, [&mx, cache](Ctx& c) {
        c.pop(), c.pop();
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word ch = c.pop();
        if (!c.ok() || !channel_ok(c, Bank::Point, ch)) return 0;
        return load_sound(c, cache, mx, Bank::Point, ch, prefix, name, {}, false,
            [&mx, ch](SoundCache::Pointer pcm) { return mx.load(Bank::Point, ch, pcm, pcm->loop, pcm->loop_start); });
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
