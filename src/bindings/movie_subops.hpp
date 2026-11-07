#pragma once
#include <functional>

#include "bindings/arc_resource_finder.hpp"
#include "video/player.hpp"
#include "vm/vm.hpp"

namespace obgi {

struct MovieHost {
    ArcResourceFinder* resources = nullptr;
    MoviePlayer* player = nullptr;
    // Plays a movie file to its end; the application decides how (window, sound device). Null = movies are skipped.
    std::function<Result<MovieEnd>(ByteView file, const std::string& name)> play;
    bool quit_requested = false; // set when the window was closed during a movie
};

// Movie sub-opcodes (analysis/视频.md):
//   91:F8 play movie (flag, name, archive) -> status. The original starts DCProcPlayMovieByMF and suspends the
//   thread until it ends; here the whole playback happens inside the call. Status: 0 played (or skipped),
//   -3 file not found, -1 playback failed. The meaning of `flag` is not known and it is ignored.
// The .bm effect-movie ops (92:F1, 90:F5..F7 ...) belong to the display objects and are not registered here.
void register_movie_subops(vm::SubopRegistry& registry, MovieHost& host);

} // namespace obgi
