#pragma once
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// "BF_Movie_______" frame sequence: 8x8 AAN-IDCT blocks with per-block replacement over the
// previous frame, plus an optional LZ / Huffman alpha plane. See analysis/BM格式.md.
//
// Frames are deltas against the persistent canvas, so they must be decoded in order starting
// from frame 0 (seeking = decode forward from the nearest frame that rewrites every block).
class BmDecoder {
  public:
    // `file` must outlive the decoder.
    static Result<BmDecoder> open(ByteView file);

    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    std::uint32_t source_bits() const { return bits_; } // 24 or 32
    std::uint32_t frame_count() const { return frame_count_; }
    std::uint32_t rate() const { return rate_; } // header field at 0x24, meaning unconfirmed (see analysis/Codex任务书4.md)

    Result<void> decode_frame(std::uint32_t index);

    // BGRA, width*height*4. Alpha is only meaningful when source_bits() == 32 (else it stays 0).
    ByteView canvas() const { return canvas_; }

  private:
    ByteView file_;
    std::uint32_t version_ = 0, width_ = 0, height_ = 0, bits_ = 0, rate_ = 0, frame_count_ = 0;
    float quant_[2][64] = {};
    std::vector<std::uint8_t> canvas_;
};

} // namespace obgi
