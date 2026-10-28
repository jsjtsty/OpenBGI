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
           }});
}

} // namespace obgi
