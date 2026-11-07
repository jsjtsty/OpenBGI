#include "video/player.hpp"
#include <chrono>
#include <thread>

namespace obgi {

Result<MovieEnd> play_movie(Renderer& renderer, Mixer& mixer, ByteView bytes, const std::string& name) {
    MoviePlayer player(renderer, mixer);
    auto started = player.start(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), name, 0);
    if (!started) return std::unexpected(started.error());
    const auto start = std::chrono::steady_clock::now();
    while (player.active()) {
        if (!renderer.poll_events()) { player.stop(MovieEnd::WindowClosed); break; }
        if (renderer.take_key() == 27) { player.stop(MovieEnd::Skipped); break; }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        if (auto updated = player.update(elapsed); !updated) return std::unexpected(updated.error());
        if (player.active()) if (auto shown = player.present(); !shown) return std::unexpected(shown.error());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return player.result().value_or(MovieEnd::Finished);
}

} // namespace obgi
