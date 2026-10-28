#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// Video stream of a media file (the shipped game's .mpg: MPEG-1 video in an MPEG program stream), decoded
// with FFmpeg to BGRA. The container and codec are detected from the bytes; see cmake/FFmpeg.cmake for what
// the build can read. Audio is not handled here: decode_audio() reads the same file's sound track.
class MovieDecoder {
  public:
    // `file` must outlive the decoder.
    static Result<std::unique_ptr<MovieDecoder>> open(ByteView file, const std::string& name = {});
    ~MovieDecoder();
    MovieDecoder(const MovieDecoder&) = delete;
    MovieDecoder& operator=(const MovieDecoder&) = delete;

    std::uint32_t width() const;
    std::uint32_t height() const;
    // Average frame rate announced by the stream (0 when unknown).
    double fps() const;

    struct Frame {
        double pts = 0;   // presentation time in seconds
        ByteView bgra;    // width*height*4, valid until the next call
    };
    // Decodes the next frame in presentation order; false at the end of the stream.
    Result<bool> next(Frame& out);

  private:
    MovieDecoder();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace obgi
