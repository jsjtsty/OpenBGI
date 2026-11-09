#include "video/player.hpp"
#include "video/movie.hpp"
#include <algorithm>
#include <chrono>
#include <thread>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace obgi {

struct MoviePlayer::Impl {
    Renderer& renderer;
    Mixer& mixer;
    OwnedBytes file;
    std::unique_ptr<MovieDecoder> decoder;
    std::shared_ptr<PcmStream> audio_stream;
    std::jthread audio_worker;
    struct VideoFrame { double pts; std::vector<std::uint8_t> pixels; };
    std::jthread video_worker;
    std::mutex video_mutex;
    std::condition_variable video_cv;
    std::deque<VideoFrame> video_frames;
    std::optional<Error> video_error, audio_error;
    struct Prepared {
        std::unique_ptr<MovieDecoder> video;
        std::unique_ptr<StreamAudioDecoder> audio;
        std::shared_ptr<PcmStream> stream;
        VideoFrame first;
        bool have_first = false;
        std::optional<Error> error;
    };
    std::jthread prepare_worker;
    std::optional<Prepared> prepared;
    std::atomic<bool> prepare_done{false};
    bool preparing = false;
    bool video_eof = false, realtime = false;
    std::size_t max_video_frames = 1;
    std::unique_ptr<Texture> texture;
    MovieDecoder::Frame pending;
    bool have_pending = false, eof = false, playing = false, audio = false;
    std::int64_t start_ms = 0, duration = 0;
    std::uint32_t width = 0, height = 0;
    std::optional<MovieRect> rect;
    double last_pts = 0, frame_time = 1.0 / 30;
    std::int64_t audio_end_wall_ms = -1;
    double audio_end_time = 0, audio_start_pts = 0;
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
bool MoviePlayer::active() const { return impl_->playing || impl_->preparing; }
bool MoviePlayer::preparing() const { return impl_->preparing; }
std::int64_t MoviePlayer::duration_ms() const { return impl_->duration; }
std::optional<MovieEnd> MoviePlayer::result() const { return impl_->end; }

void MoviePlayer::stop(MovieEnd end) {
    auto& m = *impl_;
    if (m.prepare_worker.joinable()) { m.prepare_worker.request_stop(); m.prepare_worker.join(); }
    m.preparing = false; m.prepared.reset(); m.prepare_done.store(false);
    if (m.video_worker.joinable()) { m.video_worker.request_stop(); m.video_cv.notify_all(); m.video_worker.join(); }
    if (m.audio_worker.joinable()) { m.audio_worker.request_stop(); m.audio_worker.join(); }
    { std::lock_guard lock(m.video_mutex); m.video_frames.clear(); m.video_error.reset(); m.audio_error.reset(); m.video_eof = false; }
    if (m.audio) (void)m.mixer.unload(Bank::Point, kMovieChannel);
    m.audio_stream.reset();
    m.audio = false;
    m.playing = false;
    m.end = end;
    m.texture.reset();
    m.decoder.reset();
    m.duration = 0; m.width = m.height = 0;
    m.file = {};
}

Result<void> MoviePlayer::set_volume(std::uint32_t volume) {
    if (volume > kMaxVolume) return fail(Errc::Range, "movie volume out of range");
    impl_->volume = volume;
    return impl_->audio ? impl_->mixer.set_volume(Bank::Point, kMovieChannel, volume) : Result<void>{};
}

Result<void> MoviePlayer::start(std::vector<std::uint8_t> bytes, const std::string& name, std::int64_t now, std::optional<MovieRect> rect, bool realtime) {
    auto owner = std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
    return start(OwnedBytes{owner, ByteView(*owner)}, name, now, rect, realtime);
}

Result<void> MoviePlayer::start(OwnedBytes bytes, const std::string& name, std::int64_t now, std::optional<MovieRect> rect, bool realtime) {
    const auto started = std::chrono::steady_clock::now();
    auto& m = *impl_;
    if (m.playing || m.preparing) return fail(Errc::Range, "a movie is already playing");
    stop();
    m.file = std::move(bytes);
    m.realtime = realtime;
    auto decoder = MovieDecoder::open(m.file.bytes, name);
    if (!decoder) return std::unexpected(decoder.error());
    m.decoder = std::move(*decoder);
    m.start_ms = now;
    m.duration = m.decoder->duration_ms();
    m.width = m.decoder->width(); m.height = m.decoder->height();
    const auto frame_bytes = std::uint64_t{m.width} * m.height * 4;
    m.max_video_frames = std::clamp<std::size_t>(frame_bytes ? (48u * 1024u * 1024u) / frame_bytes : 1, 1, 4);
    m.rect = rect;
    m.last_pts = 0;
    m.audio_end_wall_ms = -1; m.audio_end_time = 0;
    m.frame_time = m.decoder->fps() > 0 ? 1.0 / m.decoder->fps() : 1.0 / 30;
    m.end.reset();
    if (realtime) {
        if (auto sound = StreamAudioDecoder::open(m.file.bytes, name, m.mixer.output_rate())) {
            auto stream = std::make_shared<PcmStream>(m.mixer.output_rate());
            auto decoder = std::move(*sound);
            // Buffer a small initial segment before starting audio.
            while (stream->readable() < m.mixer.output_rate() / 10) {
                auto block = decoder->next();
                if (!block) { auto error = block.error(); stop(); return std::unexpected(error); }
                if (block->empty()) { stream->finish(); break; }
                if (stream->write(block->data(), block->size() / 2) != block->size() / 2) {
                    stop(); return fail(Errc::Range, "initial audio block exceeds stream buffer");
                }
            }
            m.audio_start_pts = decoder->first_pts();
            if (auto load = m.mixer.load_stream(Bank::Point, kMovieChannel, stream); !load) return load;
            m.audio_stream = stream; m.audio = true;
            m.audio_worker = std::jthread([decoder = std::move(decoder), stream, &m](std::stop_token stop) mutable {
                while (!stop.stop_requested()) {
                    auto block = decoder->next();
                    if (!block) {
                        std::lock_guard lock(m.video_mutex);
                        m.audio_error = block.error();
                        break;
                    }
                    if (block->empty()) break;
                    std::size_t written = 0, frames = block->size() / 2;
                    while (written < frames && !stop.stop_requested()) {
                        written += stream->write(block->data() + written * 2, frames - written);
                        if (written < frames) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
                stream->finish();
            });
        }
    } else if (auto sound = decode_audio(m.file.bytes, name)) {
        if (auto load = m.mixer.load(Bank::Point, kMovieChannel, *sound, false, 0); !load) return load;
        m.audio = true;
    }
    auto fetched = m.fetch();
    if (!fetched) { stop(); return fetched; }
    if (realtime && m.have_pending) {
        {
            std::lock_guard lock(m.video_mutex);
            m.video_frames.push_back({m.pending.pts, {m.pending.bgra.begin(), m.pending.bgra.end()}});
        }
        m.have_pending = false;
        m.video_worker = std::jthread([&m](std::stop_token stop) {
            for (;;) {
                {
                    std::unique_lock lock(m.video_mutex);
                    m.video_cv.wait(lock, [&] { return stop.stop_requested() || m.video_frames.size() < m.max_video_frames; });
                    if (stop.stop_requested()) return;
                }
                MovieDecoder::Frame frame;
                auto result = m.decoder->next(frame);
                std::lock_guard lock(m.video_mutex);
                if (!result) { m.video_error = result.error(); m.video_eof = true; return; }
                if (!*result) { m.video_eof = true; return; }
                m.video_frames.push_back({frame.pts, {frame.bgra.begin(), frame.bgra.end()}});
            }
        });
    }
    if (m.audio) if (auto play = m.mixer.play(Bank::Point, kMovieChannel, m.volume, kCenterPan); !play) { stop(); return play; }
    if (realtime) m.start_ms += std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    m.playing = true;
    return {};
}

Result<void> MoviePlayer::start_async(OwnedBytes bytes, const std::string& name, std::optional<MovieRect> rect) {
    auto& m = *impl_;
    if (m.playing || m.preparing) return fail(Errc::Range, "a movie is already playing");
    stop();
    m.file = std::move(bytes); m.rect = rect; m.realtime = true; m.preparing = true; m.end.reset();
    const auto file = m.file;
    const auto rate = m.mixer.output_rate();
    m.prepare_worker = std::jthread([&m, file, name, rate](std::stop_token cancel) {
        Impl::Prepared prepared;
        auto video = MovieDecoder::open(file.bytes, name);
        if (!video) prepared.error = video.error();
        else {
            prepared.video = std::move(*video);
            MovieDecoder::Frame frame;
            auto first = prepared.video->next(frame);
            if (!first) prepared.error = first.error();
            else if (*first) { prepared.first = {frame.pts, {frame.bgra.begin(), frame.bgra.end()}}; prepared.have_first = true; }
            if (!prepared.error && !cancel.stop_requested()) {
                if (auto audio = StreamAudioDecoder::open(file.bytes, name, rate)) {
                    prepared.audio = std::move(*audio);
                    prepared.stream = std::make_shared<PcmStream>(rate);
                    while (prepared.stream->readable() < rate / 10 && !cancel.stop_requested()) {
                        auto block = prepared.audio->next();
                        if (!block) { prepared.error = block.error(); break; }
                        if (block->empty()) { prepared.stream->finish(); break; }
                        if (prepared.stream->write(block->data(), block->size() / 2) != block->size() / 2) {
                            prepared.error = Error{Errc::Range, "initial audio block exceeds stream buffer"}; break;
                        }
                    }
                }
            }
        }
        if (cancel.stop_requested()) return;
        { std::lock_guard lock(m.video_mutex); m.prepared.emplace(std::move(prepared)); }
        m.prepare_done.store(true, std::memory_order_release);
    });
    return {};
}

Result<void> MoviePlayer::update(std::int64_t now) {
    auto& m = *impl_;
    if (m.preparing) {
        if (!m.prepare_done.load(std::memory_order_acquire)) return {};
        if (m.prepare_worker.joinable()) m.prepare_worker.join();
        Impl::Prepared prepared;
        { std::lock_guard lock(m.video_mutex); prepared = std::move(*m.prepared); m.prepared.reset(); }
        m.preparing = false;
        if (prepared.error) {
            std::fprintf(stderr, "movie preparation: %s\n", prepared.error->message.c_str());
            stop(MovieEnd::Failed);
            return {};
        }
        m.decoder = std::move(prepared.video);
        m.duration = m.decoder->duration_ms(); m.width = m.decoder->width(); m.height = m.decoder->height();
        const auto frame_bytes = std::uint64_t{m.width} * m.height * 4;
        m.max_video_frames = std::clamp<std::size_t>(frame_bytes ? (48u * 1024u * 1024u) / frame_bytes : 1, 1, 4);
        m.frame_time = m.decoder->fps() > 0 ? 1.0 / m.decoder->fps() : 1.0 / 30;
        m.start_ms = now; m.last_pts = 0; m.audio_end_wall_ms = -1; m.audio_end_time = 0; m.eof = !prepared.have_first;
        if (!prepared.have_first) m.video_eof = true;
        if (prepared.have_first) {
            { std::lock_guard lock(m.video_mutex); m.video_frames.push_back(std::move(prepared.first)); }
            m.video_worker = std::jthread([&m](std::stop_token stop) {
                for (;;) {
                    {
                        std::unique_lock lock(m.video_mutex);
                        m.video_cv.wait(lock, [&] { return stop.stop_requested() || m.video_frames.size() < m.max_video_frames; });
                        if (stop.stop_requested()) return;
                    }
                    MovieDecoder::Frame frame;
                    auto result = m.decoder->next(frame);
                    std::lock_guard lock(m.video_mutex);
                    if (!result) { m.video_error = result.error(); m.video_eof = true; return; }
                    if (!*result) { m.video_eof = true; return; }
                    m.video_frames.push_back({frame.pts, {frame.bgra.begin(), frame.bgra.end()}});
                }
            });
        }
        if (prepared.audio) {
            auto stream = std::move(prepared.stream);
            m.audio_start_pts = prepared.audio->first_pts();
            if (auto load = m.mixer.load_stream(Bank::Point, kMovieChannel, stream); !load) { stop(MovieEnd::Failed); return load; }
            m.audio_stream = stream; m.audio = true;
            m.audio_worker = std::jthread([decoder = std::move(prepared.audio), stream, &m](std::stop_token stop) mutable {
                while (!stop.stop_requested()) {
                    auto block = decoder->next();
                    if (!block) { std::lock_guard lock(m.video_mutex); m.audio_error = block.error(); break; }
                    if (block->empty()) break;
                    std::size_t written = 0, frames = block->size() / 2;
                    while (written < frames && !stop.stop_requested()) {
                        written += stream->write(block->data() + written * 2, frames - written);
                        if (written < frames) std::this_thread::sleep_for(std::chrono::milliseconds(2));
                    }
                }
                stream->finish();
            });
            if (auto play = m.mixer.play(Bank::Point, kMovieChannel, m.volume, kCenterPan); !play) { stop(MovieEnd::Failed); return play; }
        }
        m.playing = true;
    }
    if (!m.playing) return {};
    double time = double(std::max<std::int64_t>(0, now - m.start_ms)) / 1000;
    if (m.realtime && m.audio_stream) {
        const double audio_time = m.audio_start_pts + double(m.audio_stream->consumed()) / m.mixer.output_rate();
        if (m.audio_stream->finished() && m.audio_stream->readable() == 0) {
            if (m.audio_end_wall_ms < 0) { m.audio_end_wall_ms = now; m.audio_end_time = audio_time; }
            time = m.audio_end_time + double(std::max<std::int64_t>(0, now - m.audio_end_wall_ms)) / 1000;
        } else time = audio_time;
    }
    std::vector<std::uint8_t> shown;
    if (m.realtime) {
        std::lock_guard lock(m.video_mutex);
        if (m.video_error) return std::unexpected(*m.video_error);
        if (m.audio_error) return std::unexpected(*m.audio_error);
        while (!m.video_frames.empty() && m.video_frames.front().pts <= time) {
            m.last_pts = m.video_frames.front().pts;
            shown = std::move(m.video_frames.front().pixels);
            m.video_frames.pop_front();
        }
        m.eof = m.video_eof && m.video_frames.empty();
        m.video_cv.notify_one();
    } else {
        while (m.have_pending && m.pending.pts <= time) {
            shown.assign(m.pending.bgra.begin(), m.pending.bgra.end());
            m.last_pts = m.pending.pts;
            if (auto fetched = m.fetch(); !fetched) return fetched;
        }
    }
    if (!shown.empty()) {
        if (!m.texture) {
            auto texture = m.renderer.create_texture(m.width, m.height, PixelFormat::Bgra8, shown);
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
        const float width = m.rect ? float(m.rect->width) : float(m.renderer.logical_width());
        const float height = m.rect ? float(m.rect->height) : float(m.renderer.logical_height());
        const float scale = std::min(width / m.texture->width(), height / m.texture->height());
        DrawParams p;
        p.w = m.texture->width() * scale; p.h = m.texture->height() * scale;
        p.x = (m.rect ? float(m.rect->x) : 0.f) + (width - p.w) / 2;
        p.y = (m.rect ? float(m.rect->y) : 0.f) + (height - p.h) / 2;
        m.renderer.draw(*m.texture, p);
    }
    return m.renderer.end_frame();
}

} // namespace obgi
