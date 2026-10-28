#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "core/bytes.hpp"
#include "core/error.hpp"

extern "C" {
#include <libavformat/avformat.h>
}

namespace obgi {

std::string av_message(int err);
Error av_error(const char* what, int err);

// An FFmpeg demuxer reading a file that is already in memory (an archive member). The bytes must outlive it.
class AvInput {
  public:
    // `name` is an optional file name (e.g. "ac2_op.mpg"): its extension breaks ties between demuxers that probe alike.
    static Result<std::unique_ptr<AvInput>> open(ByteView file, const std::string& name = {});
    ~AvInput();
    AvInput(const AvInput&) = delete;
    AvInput& operator=(const AvInput&) = delete;

    AVFormatContext* format() const { return fmt_; }

    struct MemoryIo; // read cursor handed to the AVIO callbacks

  private:
    AvInput();
    std::unique_ptr<MemoryIo> io_;
    AVIOContext* avio_ = nullptr;
    AVFormatContext* fmt_ = nullptr;
};

} // namespace obgi
