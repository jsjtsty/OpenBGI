#pragma once
#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// "bw  " sound resource: 0x40-byte header followed by a standard Ogg Vorbis stream.
// See analysis/BW格式.md.
struct BwHeader {
    std::uint32_t sample_count = 0; // per channel
    std::uint32_t sample_rate = 0;
    std::uint32_t channels = 0;
    bool loop = false;
    std::uint32_t loop_start = 0; // in samples
    std::uint32_t model = 0;      // 0 PCM16, 1 ADPCM4, 2 HFADPCM8, 3 Ogg (all shipped files use 3)
    ByteView stream;              // payload after the header
};

Result<BwHeader> bw_parse(ByteView in);

} // namespace obgi
