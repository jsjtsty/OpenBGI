#pragma once
#include <functional>

#include "bindings/arc_resource_finder.hpp"
#include "video/player.hpp"
#include "vm/vm.hpp"

namespace obgi {

struct MovieHost {
    ArcResourceFinder* resources = nullptr;
    MoviePlayer* player = nullptr;
    bool realtime = false;
    // Plays a movie file to its end; the application decides how (window, sound device). Null = movies are skipped.
    std::function<Result<MovieEnd>(ByteView file, const std::string& name)> play;
    bool quit_requested = false; // set when the window was closed during a movie
};

// Movie sub-opcodes (analysis/视频.md):
//   92:F0 starts an archive movie in a rectangle and returns its duration in ms (0 on failure).
//   90:F1 stops playback; 90:F2 reports whether the player is active; 90:F3 sets the volume.
//   91:F8 starts a movie and suspends the thread until playback ends or is skipped. Its completion status is
//   0 for played/skipped, -3 for missing input, and -1 for a playback error. The flag remains unverified.
// The .bm effect-movie ops (92:F1, 90:F5..F7 ...) belong to the display objects and are not registered here.
void register_movie_subops(vm::SubopRegistry& registry, MovieHost& host);

} // namespace obgi
