#include "formats/bw.hpp"

#include <cstring>

namespace obgi {

namespace {
constexpr std::size_t kHeaderSize = 0x40;
}

Result<BwHeader> bw_parse(ByteView in) {
    if (in.size() < kHeaderSize) return fail(Errc::Format, "BW shorter than its header");
    const std::uint8_t* p = in.data();
    if (load_u32le(p) != kHeaderSize || std::memcmp(p + 4, "bw  ", 4) != 0)
        return fail(Errc::Format, "not a BW sound resource");
    BwHeader h;
    std::uint32_t stream_size = load_u32le(p + 0x08);
    h.sample_count = load_u32le(p + 0x0c);
    h.sample_rate = load_u32le(p + 0x10);
    h.channels = load_u32le(p + 0x14);
    h.loop = load_u32le(p + 0x18) != 0;
    h.loop_start = load_u32le(p + 0x1c);
    h.model = load_u32le(p + 0x30);
    if (stream_size > in.size() - kHeaderSize) return fail(Errc::Format, "BW stream size exceeds file");
    h.stream = in.subspan(kHeaderSize, stream_size);
    return h;
}

} // namespace obgi
