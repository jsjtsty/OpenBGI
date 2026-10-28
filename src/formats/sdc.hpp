#pragma once
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// "SDC FORMAT 1.00": keystream-masked byte-oriented LZ with sum/xor checksums (BGI.gdb, .sud blocks).
struct SdcResult {
    std::vector<std::uint8_t> data;
    std::size_t consumed = 0; // header + payload bytes, so concatenated blocks can be walked
};

bool is_sdc(ByteView in);
Result<SdcResult> sdc_decode(ByteView in);

} // namespace obgi
