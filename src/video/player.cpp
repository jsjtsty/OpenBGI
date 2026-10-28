#include "video/player.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

#include "video/movie.hpp"

namespace obgi {

namespace {

constexpr std::uint32_t kEsc = 27; // SDLK_ESCAPE
constexpr float kCanvasW = 800, kCanvasH = 600;

} // namespace

Result<MovieEnd> play_movie(Renderer& renderer, Mixer& mixer, ByteView file, const std::string& name) {
    auto decoder = MovieDecoder::open(file, name);
    if (!decoder) return std::unexpected(decoder.error());
    MovieDecoder& dec = **decoder;

    // A movie without a sound track simply plays silent.
    std::shared_ptr<Pcm> sound;
    if (auto pcm = decode_audio(file, name)) sound = *pcm;
    struct SoundGuard {
        Mixer& mixer;
        bool on;
        ~SoundGuard() {
            if (on) (void)mixer.unload(Bank::Point, kMovieChannel);
        }
    } guard{mixer, sound != nullptr};
    if (sound) {
        if (auto r = mixer.load(Bank::Point, kMovieChannel, sound, false, 0); !r) return std::unexpected(r.error());
        if (auto r = mixer.play(Bank::Point, kMovieChannel, kMaxVolume, kCenterPan); !r) return std::unexpected(r.error());
    }

    const float scale = std::min(kCanvasW / float(dec.width()), kCanvasH / float(dec.height()));
    DrawParams dp;
    dp.w = float(dec.width()) * scale;
    dp.h = float(dec.height()) * scale;
    dp.x = (kCanvasW - dp.w) * 0.5f;
    dp.y = (kCanvasH - dp.h) * 0.5f;
    const float black[4] = {0, 0, 0, 1};

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    double last_audio = 0, last_wall = 0;
    bool have_audio_clock = false;
    auto now_seconds = [&]() {
        const double wall = std::chrono::duration<double>(clock::now() - start).count();
        if (!sound) return wall;
        const std::uint32_t pos = mixer.position(Bank::Point, kMovieChannel);
        if (mixer.state(Bank::Point, kMovieChannel) == ChannelState::Playing && pos > 0) {
            last_audio = double(pos) / double(sound->sample_rate);
            last_wall = wall;
            have_audio_clock = true;
            return last_audio;
        }
        if (have_audio_clock) return last_audio + (wall - last_wall); // the sound ended: carry on by wall time
        return wall < 0.5 ? 0.0 : wall - 0.5;                         // the sound never started (no audio device)
    };

    std::unique_ptr<Texture> tex;
    std::vector<std::uint8_t> shown;
    MovieDecoder::Frame pending;
    bool have_pending = false, eof = false;
    double last_pts = 0;
    const double frame_time = dec.fps() > 0 ? 1.0 / dec.fps() : 1.0 / 30.0;
    auto fetch = [&]() -> Result<void> {
        auto more = dec.next(pending);
        if (!more) return std::unexpected(more.error());
        have_pending = *more;
        eof = !*more;
        return {};
    };
    if (auto r = fetch(); !r) return std::unexpected(r.error());

    for (;;) {
        if (!renderer.poll_events()) return MovieEnd::WindowClosed;
        if (renderer.take_key() == kEsc) return MovieEnd::Skipped;

        const double t = now_seconds();
        bool changed = false;
        while (have_pending && pending.pts <= t) {
            shown.assign(pending.bgra.begin(), pending.bgra.end());
            last_pts = pending.pts;
            changed = true;
            if (auto r = fetch(); !r) return std::unexpected(r.error());
        }
        if (changed) {
            auto created = renderer.create_texture(dec.width(), dec.height(), PixelFormat::Bgra8, shown);
            if (!created) return std::unexpected(created.error());
            tex = std::move(*created);
        }
        if (eof && !have_pending && t >= last_pts + frame_time) return MovieEnd::Finished;

        if (auto r = renderer.begin_frame(black); !r) return std::unexpected(r.error());
        if (tex) renderer.draw(*tex, dp);
        if (auto r = renderer.end_frame(); !r) return std::unexpected(r.error());
        if (!changed) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

} // namespace obgi
