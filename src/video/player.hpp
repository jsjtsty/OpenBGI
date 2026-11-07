#pragma once
#include <string>
#include <optional>

#include "audio/mixer.hpp"
#include "core/bytes.hpp"
#include "core/error.hpp"
#include "render/renderer.hpp"

namespace obgi {

// Point channel the movie's sound track plays on (the last one, which scripts do not normally use).
constexpr std::uint32_t kMovieChannel = kPointChannels - 1;

enum class MovieEnd { Finished, Skipped, WindowClosed };

// Incremental player: it never polls host events, sleeps or runs a second VM loop.
class MoviePlayer {
  public:
    MoviePlayer(Renderer& renderer, Mixer& mixer);
    ~MoviePlayer();
    Result<void> start(std::vector<std::uint8_t> bytes, const std::string& name, std::int64_t now_ms);
    Result<void> update(std::int64_t now_ms);
    Result<void> present();
    void stop(MovieEnd end = MovieEnd::Skipped);
    Result<void> set_volume(std::uint32_t volume);
    bool active() const;
    std::optional<MovieEnd> result() const;
  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Plays a movie file full screen on the renderer's logical canvas (aspect ratio kept, black bars), blocking
// until it ends, Esc skips it or the window is closed. This standalone-tool adapter
// drives MoviePlayer with a monotonic clock. EngineSession uses MoviePlayer directly.
Result<MovieEnd> play_movie(Renderer& renderer, Mixer& mixer, ByteView file, const std::string& name = {});

} // namespace obgi
