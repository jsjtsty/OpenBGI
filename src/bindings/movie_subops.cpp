#include "bindings/movie_subops.hpp"

#include <cstdio>

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
               c.pop(); // flag
               std::string name = c.cstring(c.pop());
               std::string prefix = c.cstring(c.pop());
               if (!c.ok()) return 0;
               auto file = h->resources->read(prefix, name);
               if (!file) {
                   c.push(kStatusNotFound);
                   return 0;
               }
               if (h->player) {
                   auto started = h->player->start(std::move(*file), name, c.vm.now_ms);
                   if (!started) { c.push(kStatusError); return 0; }
                   vm::WaitObject wait;
                   wait.kind = vm::WaitObject::Kind::Poll;
                   wait.cleanup = [h] { if (h->player->active()) h->player->stop(); };
                   wait.poll = [h](const vm::WaitContext& context) -> Result<vm::WaitUpdate> {
                       if (context.notified) h->player->stop(MovieEnd::Skipped);
                       return h->player->result() ? vm::WaitUpdate::complete({kStatusOk}) : vm::WaitUpdate::pending();
                   };
                   c.suspend(std::move(wait), 1);
                   return static_cast<int>(vm::StayThread);
               }
               if (!h->play) {
                   c.push(kStatusOk);
                   return 0;
               }
               auto end = h->play(*file, name);
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
