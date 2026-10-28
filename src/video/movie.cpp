#include "video/movie.hpp"

#include "audio/av_input.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
}

namespace obgi {

namespace {

struct CodecDeleter {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameDeleter {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketDeleter {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct SwsDeleter {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};

} // namespace

struct MovieDecoder::Impl {
    std::unique_ptr<AvInput> input;
    int stream = -1;
    std::unique_ptr<AVCodecContext, CodecDeleter> dec;
    std::unique_ptr<SwsContext, SwsDeleter> sws;
    std::unique_ptr<AVFrame, FrameDeleter> frame{av_frame_alloc()};
    std::unique_ptr<AVPacket, PacketDeleter> packet{av_packet_alloc()};
    std::vector<std::uint8_t> bgra;
    AVRational time_base{};
    double fps = 0;
    std::uint64_t produced = 0;
    bool flushed = false;
};

MovieDecoder::MovieDecoder() : impl_(std::make_unique<Impl>()) {}
MovieDecoder::~MovieDecoder() = default;

Result<std::unique_ptr<MovieDecoder>> MovieDecoder::open(ByteView file, const std::string& name) {
    std::unique_ptr<MovieDecoder> m(new MovieDecoder);
    Impl& s = *m->impl_;
    auto input = AvInput::open(file, name);
    if (!input) return std::unexpected(input.error());
    s.input = std::move(*input);
    AVFormatContext* fmt = s.input->format();

    const AVCodec* codec = nullptr;
    s.stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (s.stream < 0 || !codec) return fail(Errc::Unsupported, "no decodable video stream: " + av_message(s.stream < 0 ? s.stream : AVERROR_DECODER_NOT_FOUND));
    s.dec.reset(avcodec_alloc_context3(codec));
    if (!s.dec || !s.frame || !s.packet) return fail(Errc::Io, "out of memory");
    AVStream* st = fmt->streams[s.stream];
    if (int err = avcodec_parameters_to_context(s.dec.get(), st->codecpar); err < 0)
        return std::unexpected(av_error("codec parameters", err));
    if (int err = avcodec_open2(s.dec.get(), codec, nullptr); err < 0) return std::unexpected(av_error("opening the decoder", err));
    if (s.dec->width <= 0 || s.dec->height <= 0) return fail(Errc::Format, "video stream without a size");

    s.time_base = st->time_base;
    const AVRational rate = st->avg_frame_rate.num ? st->avg_frame_rate : st->r_frame_rate;
    s.fps = rate.den ? av_q2d(rate) : 0;
    s.bgra.resize(std::size_t(s.dec->width) * std::size_t(s.dec->height) * 4);
    return m;
}

std::uint32_t MovieDecoder::width() const { return static_cast<std::uint32_t>(impl_->dec->width); }
std::uint32_t MovieDecoder::height() const { return static_cast<std::uint32_t>(impl_->dec->height); }
double MovieDecoder::fps() const { return impl_->fps; }

Result<bool> MovieDecoder::next(Frame& out) {
    Impl& s = *impl_;
    for (;;) {
        const int r = avcodec_receive_frame(s.dec.get(), s.frame.get());
        if (r == 0) {
            // YUV 4:2:0 -> BGRA; swscale's default matrix for untagged video is BT.601, which is what MPEG-1 uses
            s.sws.reset(sws_getCachedContext(s.sws.release(), s.frame->width, s.frame->height, static_cast<AVPixelFormat>(s.frame->format),
                                             s.frame->width, s.frame->height, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr));
            if (!s.sws) return fail(Errc::Unsupported, "cannot convert the video pixel format");
            if (std::size_t(s.frame->width) * std::size_t(s.frame->height) * 4 != s.bgra.size())
                return fail(Errc::Format, "video frame size changed mid-stream");
            std::uint8_t* dst[4] = {s.bgra.data(), nullptr, nullptr, nullptr};
            const int dst_stride[4] = {s.frame->width * 4, 0, 0, 0};
            sws_scale(s.sws.get(), s.frame->data, s.frame->linesize, 0, s.frame->height, dst, dst_stride);
            const std::int64_t ts = s.frame->best_effort_timestamp;
            out.pts = ts == AV_NOPTS_VALUE ? (s.fps > 0 ? double(s.produced) / s.fps : 0.0) : double(ts) * av_q2d(s.time_base);
            out.bgra = s.bgra;
            ++s.produced;
            av_frame_unref(s.frame.get());
            return true;
        }
        if (r == AVERROR_EOF) return false;
        if (r != AVERROR(EAGAIN)) return std::unexpected(av_error("decoding video", r));

        // the decoder wants more input
        if (s.flushed) return false;
        for (;;) {
            const int rd = av_read_frame(s.input->format(), s.packet.get());
            if (rd < 0) {
                avcodec_send_packet(s.dec.get(), nullptr);
                s.flushed = true;
                break;
            }
            if (s.packet->stream_index != s.stream) {
                av_packet_unref(s.packet.get());
                continue;
            }
            const int sd = avcodec_send_packet(s.dec.get(), s.packet.get());
            av_packet_unref(s.packet.get());
            if (sd < 0 && sd != AVERROR(EAGAIN)) return std::unexpected(av_error("decoding video", sd));
            break;
        }
    }
}

} // namespace obgi
