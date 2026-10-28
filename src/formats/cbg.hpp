#pragma once
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// "CompressedBG___" version 1: keystream-masked frequency table, Huffman, zero-run RLE, then
// left/above averaging prediction. See analysis/CBG格式.md.
struct CbgImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t bits_per_pixel = 0; // 8, 24 or 32
    std::vector<std::uint8_t> pixels; // native layout: gray, or B,G,R[,A] per pixel; no row padding

    std::uint32_t channels() const { return bits_per_pixel / 8; }
    // 8-bit RGBA, alpha 255 when the source has none, gray replicated.
    std::vector<std::uint8_t> to_rgba() const;
};

bool is_cbg(ByteView in);
Result<CbgImage> cbg_decode(ByteView in);

} // namespace obgi
