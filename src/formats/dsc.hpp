#pragma once
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {

// "DSC FORMAT 1.00": keystream-obfuscated canonical-Huffman + LZ container.
// See analysis/DSC格式.md for the format description.
bool is_dsc(ByteView in);
Result<std::vector<std::uint8_t>> dsc_decode(ByteView in);

} // namespace obgi
