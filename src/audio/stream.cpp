#include "audio/stream.hpp"
#include "audio/av_input.hpp"
#include <algorithm>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
}
namespace obgi {
std::size_t PcmStream::readable() const {
    const auto read = read_.load(std::memory_order_acquire);
    const auto written = write_.load(std::memory_order_acquire);
    return static_cast<std::size_t>(written - read);
}
std::size_t PcmStream::writable() const { return capacity_ - readable(); }
std::size_t PcmStream::write(const std::int16_t* in, std::size_t frames) {
    auto w = write_.load(std::memory_order_relaxed);
    frames = std::min(frames, capacity_ - static_cast<std::size_t>(w - read_.load(std::memory_order_acquire)));
    for (std::size_t i = 0; i < frames; ++i) {
        const auto p = ((w + i) % capacity_) * 2;
        samples_[p] = in[i * 2]; samples_[p + 1] = in[i * 2 + 1];
    }
    write_.store(w + frames, std::memory_order_release);
    return frames;
}
bool PcmStream::read(std::int16_t& left, std::int16_t& right) {
    const auto r = read_.load(std::memory_order_relaxed);
    if (r == write_.load(std::memory_order_acquire)) return false;
    const auto p = (r % capacity_) * 2;
    left = samples_[p]; right = samples_[p + 1];
    read_.store(r + 1, std::memory_order_release);
    consumed_.fetch_add(1, std::memory_order_relaxed);
    return true;
}
struct StreamAudioDecoder::Impl {
    std::unique_ptr<AvInput> input;
    AVCodecContext* codec = nullptr;
    SwrContext* swr = nullptr;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    int stream = -1;
    bool flushing = false, done = false, have_first_pts = false;
    double first_pts = 0;
    ~Impl() { av_packet_free(&packet); av_frame_free(&frame); swr_free(&swr); avcodec_free_context(&codec); }
};
StreamAudioDecoder::StreamAudioDecoder() : impl_(std::make_unique<Impl>()) {}
StreamAudioDecoder::~StreamAudioDecoder() = default;
Result<std::unique_ptr<StreamAudioDecoder>> StreamAudioDecoder::open(ByteView file, const std::string& name, std::uint32_t rate) {
    auto d = std::unique_ptr<StreamAudioDecoder>(new StreamAudioDecoder);
    auto& s = *d->impl_;
    auto input = AvInput::open(file, name);
    if (!input) return std::unexpected(input.error());
    s.input = std::move(*input);
    const AVCodec* codec = nullptr;
    auto* format = s.input->format();
    s.stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (s.stream < 0 || !codec) return fail(Errc::Unsupported, "no decodable audio stream");
    s.codec = avcodec_alloc_context3(codec);
    if (!s.codec || !s.frame || !s.packet) return fail(Errc::Io, "out of memory");
    int err = avcodec_parameters_to_context(s.codec, format->streams[s.stream]->codecpar);
    if (err < 0) return std::unexpected(av_error("audio codec parameters", err));
    if ((err = avcodec_open2(s.codec, codec, nullptr)) < 0) return std::unexpected(av_error("opening audio decoder", err));
    if (s.codec->sample_rate <= 0 || s.codec->ch_layout.nb_channels < 1) return fail(Errc::Format, "invalid audio format");
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    err = swr_alloc_set_opts2(&s.swr, &stereo, AV_SAMPLE_FMT_S16, static_cast<int>(rate), &s.codec->ch_layout, s.codec->sample_fmt, s.codec->sample_rate, 0, nullptr);
    if (err < 0 || !s.swr || (err = swr_init(s.swr)) < 0) return std::unexpected(av_error("audio resampler", err));
    return d;
}
double StreamAudioDecoder::first_pts() const { return impl_->first_pts; }
Result<std::vector<std::int16_t>> StreamAudioDecoder::next() {
    auto& s = *impl_;
    if (s.done) return std::vector<std::int16_t>{};
    for (;;) {
        const int r = avcodec_receive_frame(s.codec, s.frame);
        if (r == 0) {
            if (!s.have_first_pts) {
                const auto ts = s.frame->best_effort_timestamp;
                if (ts != AV_NOPTS_VALUE) s.first_pts = double(ts) * av_q2d(s.input->format()->streams[s.stream]->time_base);
                s.have_first_pts = true;
            }
            int cap = swr_get_out_samples(s.swr, s.frame->nb_samples);
            if (cap < 0) return std::unexpected(av_error("audio resampler", cap));
            std::vector<std::int16_t> out(static_cast<std::size_t>(cap) * 2);
            std::uint8_t* dst[] = {reinterpret_cast<std::uint8_t*>(out.data())};
            const int n = swr_convert(s.swr, dst, cap, const_cast<const std::uint8_t**>(s.frame->extended_data), s.frame->nb_samples);
            av_frame_unref(s.frame);
            if (n < 0) return std::unexpected(av_error("audio resampling", n));
            out.resize(static_cast<std::size_t>(n) * 2);
            if (!out.empty()) return out;
            continue;
        }
        if (r == AVERROR_EOF) {
            const int cap = swr_get_out_samples(s.swr, 0);
            std::vector<std::int16_t> out(static_cast<std::size_t>(std::max(cap, 0)) * 2);
            std::uint8_t* dst[] = {reinterpret_cast<std::uint8_t*>(out.data())};
            const int n = cap > 0 ? swr_convert(s.swr, dst, cap, nullptr, 0) : 0;
            if (n < 0) return std::unexpected(av_error("audio resampler flush", n));
            out.resize(static_cast<std::size_t>(n) * 2);
            s.done = true; return out;
        }
        if (r != AVERROR(EAGAIN)) return std::unexpected(av_error("decoding audio", r));
        if (s.flushing) { s.done = true; return std::vector<std::int16_t>{}; }
        for (;;) {
            int got = av_read_frame(s.input->format(), s.packet);
            if (got < 0) { avcodec_send_packet(s.codec, nullptr); s.flushing = true; break; }
            if (s.packet->stream_index != s.stream) { av_packet_unref(s.packet); continue; }
            const int sent = avcodec_send_packet(s.codec, s.packet);
            av_packet_unref(s.packet);
            if (sent < 0 && sent != AVERROR(EAGAIN)) return std::unexpected(av_error("feeding audio decoder", sent));
            break;
        }
    }
}
}
