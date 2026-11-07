#include "video/player.hpp"
#include "video/movie.hpp"
#include <algorithm>

namespace obgi {

struct MoviePlayer::Impl {
    Renderer& renderer;
    Mixer& mixer;
    std::vector<std::uint8_t> file;
    std::unique_ptr<MovieDecoder> decoder;
    std::unique_ptr<Texture> texture;
    MovieDecoder::Frame pending;
    bool have_pending = false, eof = false, playing = false, audio = false;
    std::int64_t start_ms = 0;
    double last_pts = 0, frame_time = 1.0 / 30;
    std::optional<MovieEnd> end;
    std::uint32_t volume = kMaxVolume;
    Result<void> fetch() {
        auto more = decoder->next(pending);
        if (!more) return std::unexpected(more.error());
        have_pending = *more;
        eof = !*more;
        return {};
    }
};

MoviePlayer::MoviePlayer(Renderer& renderer, Mixer& mixer) : impl_(std::make_unique<Impl>(renderer, mixer)) {}
MoviePlayer::~MoviePlayer() { stop(); }
bool MoviePlayer::active() const { return impl_->playing; }
std::optional<MovieEnd> MoviePlayer::result() const { return impl_->end; }

void MoviePlayer::stop(MovieEnd end) {
    auto& m = *impl_;
    if (m.audio) (void)m.mixer.unload(Bank::Point, kMovieChannel);
    m.audio = false;
    m.playing = false;
    m.end = end;
    m.texture.reset();
    m.decoder.reset();
    std::vector<std::uint8_t>().swap(m.file);
}

Result<void> MoviePlayer::set_volume(std::uint32_t volume) {
    if (volume > kMaxVolume) return fail(Errc::Range, "movie volume out of range");
    impl_->volume = volume;
    return impl_->audio ? impl_->mixer.set_volume(Bank::Point, kMovieChannel, volume) : Result<void>{};
}

Result<void> MoviePlayer::start(std::vector<std::uint8_t> bytes, const std::string& name, std::int64_t now) {
    auto& m = *impl_;
    if (m.playing) return fail(Errc::Range, "a movie is already playing");
    stop();
    m.file = std::move(bytes);
    auto decoder = MovieDecoder::open(m.file, name);
    if (!decoder) return std::unexpected(decoder.error());
    m.decoder = std::move(*decoder);
    m.start_ms = now;
    m.last_pts = 0;
    m.frame_time = m.decoder->fps() > 0 ? 1.0 / m.decoder->fps() : 1.0 / 30;
    m.end.reset();
    if (auto sound = decode_audio(m.file, name)) {
        if (auto load = m.mixer.load(Bank::Point, kMovieChannel, *sound, false, 0); !load) return load;
        m.audio = true;
        if (auto play = m.mixer.play(Bank::Point, kMovieChannel, m.volume, kCenterPan); !play) return play;
    }
    auto fetched = m.fetch();
    if (!fetched) { stop(); return fetched; }
    m.playing = true;
    return {};
}

Result<void> MoviePlayer::update(std::int64_t now) {
    auto& m = *impl_;
    if (!m.playing) return {};
    const double time = double(std::max<std::int64_t>(0, now - m.start_ms)) / 1000;
    std::vector<std::uint8_t> shown;
    while (m.have_pending && m.pending.pts <= time) {
        shown.assign(m.pending.bgra.begin(), m.pending.bgra.end());
        m.last_pts = m.pending.pts;
        if (auto fetched = m.fetch(); !fetched) return fetched;
    }
    if (!shown.empty()) {
        if (!m.texture) {
            auto texture = m.renderer.create_texture(m.decoder->width(), m.decoder->height(), PixelFormat::Bgra8, shown);
            if (!texture) return std::unexpected(texture.error());
            m.texture = std::move(*texture);
        } else if (auto uploaded = m.renderer.update_texture(*m.texture, shown); !uploaded) return uploaded;
    }
    if (m.eof && time >= m.last_pts + m.frame_time) stop(MovieEnd::Finished);
    return {};
}

Result<void> MoviePlayer::present() {
    auto& m = *impl_;
    const float black[] = {0, 0, 0, 1};
    if (auto frame = m.renderer.begin_frame(black); !frame) return frame;
    if (m.texture) {
        const float width = static_cast<float>(m.renderer.logical_width()), height = static_cast<float>(m.renderer.logical_height());
        const float scale = std::min(width / m.texture->width(), height / m.texture->height());
        DrawParams p;
        p.w = m.texture->width() * scale; p.h = m.texture->height() * scale;
        p.x = (width - p.w) / 2; p.y = (height - p.h) / 2;
        m.renderer.draw(*m.texture, p);
    }
    return m.renderer.end_frame();
}

} // namespace obgi
