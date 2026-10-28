#include "audio/av_input.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/log.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace obgi {

std::string av_message(int err) {
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

Error av_error(const char* what, int err) { return Error{Errc::Format, std::string(what) + ": " + av_message(err)}; }

struct AvInput::MemoryIo {
    const std::uint8_t* data;
    std::size_t size;
    std::size_t pos = 0;
};

namespace {

int read_packet(void* opaque, std::uint8_t* buf, int n) {
    auto& io = *static_cast<AvInput::MemoryIo*>(opaque);
    if (io.pos >= io.size) return AVERROR_EOF;
    const std::size_t take = std::min<std::size_t>(static_cast<std::size_t>(n), io.size - io.pos);
    std::memcpy(buf, io.data + io.pos, take);
    io.pos += take;
    return static_cast<int>(take);
}

std::int64_t seek_packet(void* opaque, std::int64_t offset, int whence) {
    auto& io = *static_cast<AvInput::MemoryIo*>(opaque);
    if (whence & AVSEEK_SIZE) return static_cast<std::int64_t>(io.size);
    std::int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
    case SEEK_SET: base = 0; break;
    case SEEK_CUR: base = static_cast<std::int64_t>(io.pos); break;
    case SEEK_END: base = static_cast<std::int64_t>(io.size); break;
    default: return -1;
    }
    const std::int64_t target = base + offset;
    if (target < 0 || target > static_cast<std::int64_t>(io.size)) return -1;
    io.pos = static_cast<std::size_t>(target);
    return target;
}

} // namespace

AvInput::AvInput() = default;

Result<std::unique_ptr<AvInput>> AvInput::open(ByteView file, const std::string& name) {
    // OBGI_FFMPEG_LOG=verbose|debug turns FFmpeg's own diagnostics up (development aid)
    if (const char* level = std::getenv("OBGI_FFMPEG_LOG")) av_log_set_level(std::string(level) == "debug" ? AV_LOG_DEBUG : AV_LOG_VERBOSE);
    std::unique_ptr<AvInput> in(new AvInput);
    in->io_ = std::make_unique<MemoryIo>(MemoryIo{file.data(), file.size()});
    constexpr int kBuffer = 1 << 15;
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(kBuffer));
    if (!buffer) return fail(Errc::Io, "out of memory");
    in->avio_ = avio_alloc_context(buffer, kBuffer, 0, in->io_.get(), read_packet, nullptr, seek_packet);
    if (!in->avio_) {
        av_free(buffer);
        return fail(Errc::Io, "out of memory");
    }
    AVFormatContext* raw = avformat_alloc_context();
    if (!raw) return fail(Errc::Io, "out of memory");
    raw->pb = in->avio_;
    raw->flags |= AVFMT_FLAG_CUSTOM_IO;
    // on failure avformat_open_input frees `raw`; the custom AVIO stays ours either way
    // MPEG program streams can carry a long run of one stream before the other shows up
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "probesize", "33554432", 0);
    av_dict_set(&opts, "analyzeduration", "10000000", 0);
    const int open_err = avformat_open_input(&raw, name.empty() ? nullptr : name.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (int err = open_err; err < 0)
        return std::unexpected(av_error("not a media file FFmpeg can open", err));
    in->fmt_ = raw;
    if (int err = avformat_find_stream_info(in->fmt_, nullptr); err < 0) return std::unexpected(av_error("stream info", err));
    return in;
}

AvInput::~AvInput() {
    if (fmt_) avformat_close_input(&fmt_);
    if (avio_) {
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
    }
}

} // namespace obgi
