#pragma once
#include <string>

#include "audio/mixer.hpp"
#include "core/bytes.hpp"
#include "core/error.hpp"
#include "render/renderer.hpp"

namespace obgi {

// Point channel the movie's sound track plays on (the last one, which scripts do not normally use).
constexpr std::uint32_t kMovieChannel = kPointChannels - 1;

enum class MovieEnd { Finished, Skipped, WindowClosed };

// Plays a movie file full screen on the renderer's logical canvas (aspect ratio kept, black bars), blocking
// until it ends, Esc skips it or the window is closed. Sound goes through `mixer`, which must be running on
// an audio device for the picture to follow the sound; video is shown by the sound clock while the sound
// plays and by the wall clock otherwise. Frames that come due late are dropped, never queued.
Result<MovieEnd> play_movie(Renderer& renderer, Mixer& mixer, ByteView file, const std::string& name = {});

} // namespace obgi
