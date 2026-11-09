#include "bindings/movie_subops.hpp"

#include <cstdio>
#include <cstdlib>
#include <chrono>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

constexpr Word kStatusOk = 0;
constexpr Word kStatusError = static_cast<Word>(-1);
constexpr Word kStatusNotFound = static_cast<Word>(-3);

} // namespace

void register_movie_subops(vm::SubopRegistry& r, MovieHost& host) {
    MovieHost* h = &host;
    r.add({0x92F0, "play archive movie", 6, 1, [h](Ctx& c) {
        const auto begin = std::chrono::steady_clock::now();
        const auto height = static_cast<std::int32_t>(c.pop());
        const auto width = static_cast<std::int32_t>(c.pop());
        const auto y = static_cast<std::int32_t>(c.pop());
        const auto x = static_cast<std::int32_t>(c.pop());
        const std::string name = c.cstring(c.pop());
        const std::string prefix = c.cstring(c.pop());
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_PERF")) std::fprintf(stderr, "movie request %s/%s rect=%d,%d %dx%d\n", prefix.c_str(), name.c_str(), x, y, width, height);
        if (width <= 0 || height <= 0) { c.fail(Errc::Range, "invalid movie size"); return 0; }
        if (!h->player || !h->resources || name.empty()) { c.push(0); return 0; }
        auto file = h->resources->open_media(prefix, name);
        if (!file) { c.push(0); return 0; }
        const auto read_ms = h->realtime ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() : 0;
        if (h->realtime) {
            auto started = h->player->start_async(std::move(*file), name, MovieRect{x, y, width, height});
            if (!started) { c.push(0); return 0; }
            vm::WaitObject wait;
            wait.kind = vm::WaitObject::Kind::Poll;
            wait.cleanup = [h] { if (h->player->preparing()) h->player->stop(); };
            wait.poll = [h](const vm::WaitContext&) -> Result<vm::WaitUpdate> {
                if (h->player->preparing()) return vm::WaitUpdate::pending();
                const auto duration = h->player->duration_ms();
                const Word result = h->player->active() ? (duration > 0 && duration <= 0xffffffffLL ? static_cast<Word>(duration) : 1u) : 0u;
                return vm::WaitUpdate::complete({result});
            };
            c.suspend(std::move(wait), 1);
            return static_cast<int>(vm::StayThread);
        }
        auto started = h->player->start(std::move(*file), name, c.vm.now_ms + read_ms, MovieRect{x, y, width, height}, false);
        if (!started) {
            std::fprintf(stderr, "movie %s/%s: %s\n", prefix.c_str(), name.c_str(), started.error().message.c_str());
            c.push(0); return 0;
        }
        const auto duration = h->player->duration_ms();
        c.push(duration > 0 && duration <= 0xffffffffLL ? static_cast<Word>(duration) : 1u);
        return 0;
    }, 1});
    r.add({0x90F1, "stop movie", 0, 0, [h](Ctx&) {
        if (h->player && h->player->active()) h->player->stop();
        return 0;
    }});
    r.add({0x90F2, "movie playing", 0, 1, [h](Ctx& c) {
        c.push(h->player && h->player->active() ? 1 : 0);
        return 0;
    }});
    r.add({0x90F3, "movie volume", 1, 0, [h](Ctx& c) {
        const auto volume = c.pop();
        if (volume > kMaxVolume) c.fail(Errc::Range, "movie volume out of range");
        if (c.ok() && h->player) {
            if (auto set = h->player->set_volume(volume); !set) c.fail(set.error().code, set.error().message);
        }
        return 0;
    }});
    r.add({0x91F8, "play movie", 3, 1, [h](Ctx& c) {
               const auto begin = std::chrono::steady_clock::now();
               c.pop(); // flag
               std::string name = c.cstring(c.pop());
               std::string prefix = c.cstring(c.pop());
               if (!c.ok()) return 0;
               auto file = h->resources->open_media(prefix, name);
               if (!file) {
                   c.push(kStatusNotFound);
                   return 0;
               }
               if (h->player) {
                   const auto read_ms = h->realtime ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin).count() : 0;
                   auto started = h->realtime ? h->player->start_async(std::move(*file), name)
                                              : h->player->start(std::move(*file), name, c.vm.now_ms + read_ms, std::nullopt, false);
                   if (!started) { c.push(kStatusError); return 0; }
                   vm::WaitObject wait;
                   wait.kind = vm::WaitObject::Kind::Poll;
                   wait.cleanup = [h] { if (h->player->active()) h->player->stop(); };
                   wait.poll = [h](const vm::WaitContext& context) -> Result<vm::WaitUpdate> {
                       if (context.notified) h->player->stop(MovieEnd::Skipped);
                       const auto result = h->player->result();
                       return result ? vm::WaitUpdate::complete({*result == MovieEnd::Failed ? kStatusError : kStatusOk}) : vm::WaitUpdate::pending();
                   };
                   c.suspend(std::move(wait), 1);
                   return static_cast<int>(vm::StayThread);
               }
               if (!h->play) {
                   c.push(kStatusOk);
                   return 0;
               }
               auto end = h->play(file->bytes, name);
               if (!end) {
                   std::fprintf(stderr, "movie %s/%s: %s\n", prefix.c_str(), name.c_str(), end.error().message.c_str());
                   c.push(kStatusError);
                   return 0;
               }
               if (*end == MovieEnd::WindowClosed) h->quit_requested = true;
               c.push(kStatusOk);
               return 0;
           }, 1});
}

} // namespace obgi
