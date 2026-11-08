#include "audio/pcm.hpp"

#include <algorithm>
#include <cstring>
#include <string>

#include "audio/av_input.hpp"
#include "formats/bw.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace obgi {

namespace {

struct CodecDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct SwrDeleter {
    void operator()(SwrContext* s) const { swr_free(&s); }
};
struct FrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};

} // namespace

const char* ffmpeg_license() { return avutil_license(); }

Result<std::shared_ptr<Pcm>> decode_audio(ByteView file, const std::string& name) {
    auto input = AvInput::open(file, name);
    if (!input) return std::unexpected(input.error());
    AVFormatContext* fmt = (*input)->format();
    int err = 0;

    const AVCodec* codec = nullptr;
    const int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (stream < 0 || !codec) return fail(Errc::Unsupported, "no decodable audio stream");
    std::unique_ptr<AVCodecContext, CodecDeleter> dec(avcodec_alloc_context3(codec));
    if (!dec) return fail(Errc::Io, "out of memory");
    if (err = avcodec_parameters_to_context(dec.get(), fmt->streams[stream]->codecpar); err < 0)
        return std::unexpected(av_error("codec parameters", err));
    dec->pkt_timebase = fmt->streams[stream]->time_base;
    if (err = avcodec_open2(dec.get(), codec, nullptr); err < 0) return std::unexpected(av_error("opening the decoder", err));

    const int in_channels = dec->ch_layout.nb_channels;
    if (in_channels < 1 || dec->sample_rate <= 0) return fail(Errc::Format, "audio stream without a usable format");
    const int out_channels = in_channels == 1 ? 1 : 2;
    AVChannelLayout out_layout{};
    av_channel_layout_default(&out_layout, out_channels);
    SwrContext* swr_raw = nullptr;
    err = swr_alloc_set_opts2(&swr_raw, &out_layout, AV_SAMPLE_FMT_S16, dec->sample_rate, &dec->ch_layout, dec->sample_fmt,
                              dec->sample_rate, 0, nullptr);
    std::unique_ptr<SwrContext, SwrDeleter> swr(swr_raw);
    if (err < 0 || !swr || (err = swr_init(swr.get())) < 0) return std::unexpected(av_error("resampler", err));

    auto pcm = std::make_shared<Pcm>();
    pcm->sample_rate = static_cast<std::uint32_t>(dec->sample_rate);
    pcm->channels = static_cast<std::uint32_t>(out_channels);

    std::vector<std::uint8_t> staging;
    auto append = [&](const std::uint8_t* const* in, int in_samples) -> Result<void> {
        const int cap = swr_get_out_samples(swr.get(), in_samples);
        if (cap < 0) return std::unexpected(av_error("resampler", cap));
        staging.resize(std::size_t(cap) * out_channels * sizeof(std::int16_t));
        std::uint8_t* out[1] = {staging.data()};
        const int n = swr_convert(swr.get(), out, cap, in, in_samples);
        if (n < 0) return std::unexpected(av_error("resampling", n));
        const auto* s = reinterpret_cast<const std::int16_t*>(staging.data());
        pcm->samples.insert(pcm->samples.end(), s, s + std::size_t(n) * out_channels);
        return {};
    };

    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    auto drain = [&]() -> Result<void> {
        for (;;) {
            const int r = avcodec_receive_frame(dec.get(), frame.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return {};
            if (r < 0) return std::unexpected(av_error("decoding", r));
            auto ok = append(frame->extended_data, frame->nb_samples);
            av_frame_unref(frame.get());
            if (!ok) return ok;
        }
    };
    while (av_read_frame(fmt, packet.get()) >= 0) {
        if (packet->stream_index == stream) {
            const int r = avcodec_send_packet(dec.get(), packet.get());
            if (r < 0 && r != AVERROR(EAGAIN)) {
                av_packet_unref(packet.get());
                return std::unexpected(av_error("decoding", r));
            }
            if (auto ok = drain(); !ok) {
                av_packet_unref(packet.get());
                return std::unexpected(ok.error());
            }
        }
        av_packet_unref(packet.get());
    }
    avcodec_send_packet(dec.get(), nullptr); // flush the decoder
    if (auto ok = drain(); !ok) return std::unexpected(ok.error());
    if (auto ok = append(nullptr, 0); !ok) return std::unexpected(ok.error()); // flush the resampler
    if (pcm->samples.empty()) return fail(Errc::Format, "audio stream decoded to nothing");
    return pcm;
}

Result<std::shared_ptr<Pcm>> decode_bw(ByteView file) {
    auto header = bw_parse(file);
    if (!header) return std::unexpected(header.error());
    auto pcm = decode_audio(header->stream);
    if (!pcm) return std::unexpected(pcm.error());
    (*pcm)->loop = header->loop;
    (*pcm)->loop_start = header->loop_start;
    if ((*pcm)->loop_start >= (*pcm)->frames()) (*pcm)->loop_start = 0;
    return pcm;
}

Result<std::shared_ptr<Pcm>> join_intro_loop(const Pcm& intro, const Pcm& body) {
    if (intro.sample_rate != body.sample_rate || intro.channels != body.channels)
        return fail(Errc::Format, "intro and loop sound files differ in format");
    auto out = std::make_shared<Pcm>();
    out->sample_rate = intro.sample_rate;
    out->channels = intro.channels;
    out->samples = intro.samples;
    out->samples.insert(out->samples.end(), body.samples.begin(), body.samples.end());
    out->loop = true;
    out->loop_start = intro.frames();
    return out;
}

} // namespace obgi
