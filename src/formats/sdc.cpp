#include "formats/sdc.hpp"

#include <algorithm>
#include <cstring>

#include "formats/keystream.hpp"

namespace obgi {

namespace {
constexpr char kMagic[16] = {'S', 'D', 'C', ' ', 'F', 'O', 'R', 'M', 'A', 'T', ' ', '1', '.', '0', '0', 0};
constexpr std::size_t kHeaderSize = 0x20;
constexpr std::uint32_t kMaxOutput = 0x10000000;
} // namespace

bool is_sdc(ByteView in) { return in.size() >= sizeof kMagic && std::memcmp(in.data(), kMagic, sizeof kMagic) == 0; }

Result<SdcResult> sdc_decode(ByteView in) {
    if (!is_sdc(in)) return fail(Errc::Format, "not an SDC stream");
    if (in.size() < kHeaderSize) return fail(Errc::Format, "truncated SDC header");
    std::uint32_t seed = load_u32le(in.data() + 0x10);
    std::uint32_t csize = load_u32le(in.data() + 0x14);
    std::uint32_t osize = load_u32le(in.data() + 0x18);
    std::uint16_t sum_expected, xor_expected;
    std::memcpy(&sum_expected, in.data() + 0x1c, 2);
    std::memcpy(&xor_expected, in.data() + 0x1e, 2);
    if (osize > kMaxOutput) return fail(Errc::Range, "SDC output too large");
    if (in.size() - kHeaderSize < csize) return fail(Errc::Format, "truncated SDC payload");

    ByteView payload = in.subspan(kHeaderSize, csize);
    std::uint16_t sum = 0, xr = 0;
    for (auto c : payload) {
        sum = static_cast<std::uint16_t>(sum + c);
        xr ^= c;
    }
    if (sum != sum_expected || xr != xor_expected) return fail(Errc::Format, "SDC checksum mismatch");

    Keystream ks(seed);
    std::vector<std::uint8_t> plain(csize);
    for (std::size_t i = 0; i < csize; ++i) plain[i] = static_cast<std::uint8_t>(payload[i] - ks.next());

    SdcResult r;
    r.consumed = kHeaderSize + csize;
    auto& out = r.data;
    out.reserve(osize);
    std::size_t i = 0;
    while (i < plain.size()) {
        std::uint8_t c = plain[i++];
        if (c & 0x80) {
            if (i >= plain.size()) return fail(Errc::Format, "SDC truncated match");
            std::size_t n = ((c >> 3) & 0xf) + 2;
            std::size_t back = plain[i++] + 2 + (static_cast<std::size_t>(c & 7) << 8);
            if (back > out.size()) return fail(Errc::Format, "SDC back-reference before start");
            for (std::size_t k = 0; k < n; ++k) out.push_back(out[out.size() - back]);
        } else {
            std::size_t n = static_cast<std::size_t>(c) + 1;
            if (n > plain.size() - i) return fail(Errc::Format, "SDC truncated literal");
            out.insert(out.end(), plain.begin() + i, plain.begin() + i + n);
            i += n;
        }
    }
    if (out.size() != osize) return fail(Errc::Format, "SDC decoded size mismatch");
    return r;
}

std::vector<std::uint8_t> sdc_encode(ByteView in, std::uint32_t seed) {
    // literal runs only (up to 128 bytes each): valid SDC, just not compressed
    std::vector<std::uint8_t> plain;
    for (std::size_t at = 0; at < in.size();) {
        const std::size_t n = std::min<std::size_t>(128, in.size() - at);
        plain.push_back(static_cast<std::uint8_t>(n - 1));
        plain.insert(plain.end(), in.begin() + at, in.begin() + at + n);
        at += n;
    }
    std::vector<std::uint8_t> out(kHeaderSize + plain.size());
    std::memcpy(out.data(), kMagic, sizeof kMagic);
    Keystream ks(seed);
    std::uint16_t sum = 0, xr = 0;
    for (std::size_t i = 0; i < plain.size(); ++i) {
        const auto b = static_cast<std::uint8_t>(plain[i] + ks.next());
        out[kHeaderSize + i] = b;
        sum = static_cast<std::uint16_t>(sum + b);
        xr ^= b;
    }
    const std::uint32_t csize = static_cast<std::uint32_t>(plain.size()), osize = static_cast<std::uint32_t>(in.size());
    std::memcpy(out.data() + 0x10, &seed, 4);
    std::memcpy(out.data() + 0x14, &csize, 4);
    std::memcpy(out.data() + 0x18, &osize, 4);
    std::memcpy(out.data() + 0x1c, &sum, 2);
    std::memcpy(out.data() + 0x1e, &xr, 2);
    return out;
}

} // namespace obgi
