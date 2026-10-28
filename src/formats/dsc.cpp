#include "formats/dsc.hpp"

#include <array>
#include <cstring>

#include "formats/keystream.hpp"

namespace obgi {

namespace {

constexpr char kMagic[16] = {'D', 'S', 'C', ' ', 'F', 'O', 'R', 'M', 'A', 'T', ' ', '1', '.', '0', '0', 0};
constexpr std::size_t kLengthsOffset = 0x20;
constexpr std::size_t kSymbolCount = 512;
constexpr std::size_t kStreamOffset = kLengthsOffset + kSymbolCount;
constexpr std::uint32_t kMaxOutput = 0x4000000;
constexpr int kMaxCodeLength = 32;

// MSB-first bit reader; reading past the end sets `overrun` and yields zeros.
struct BitReader {
    ByteView data;
    std::size_t bit = 0;
    bool overrun = false;

    unsigned get1() {
        std::size_t byte = bit >> 3;
        if (byte >= data.size()) {
            overrun = true;
            return 0;
        }
        unsigned v = (data[byte] >> (7 - (bit & 7))) & 1u;
        ++bit;
        return v;
    }
    unsigned get(int n) {
        unsigned v = 0;
        while (n-- > 0) v = (v << 1) | get1();
        return v;
    }
};

} // namespace

bool is_dsc(ByteView in) { return in.size() >= sizeof kMagic && std::memcmp(in.data(), kMagic, sizeof kMagic) == 0; }

Result<std::vector<std::uint8_t>> dsc_decode(ByteView in) {
    if (!is_dsc(in)) return fail(Errc::Format, "not a DSC stream");
    if (in.size() < kStreamOffset) return fail(Errc::Format, "truncated DSC header");

    std::uint32_t seed = load_u32le(in.data() + 0x10);
    std::uint32_t out_size = load_u32le(in.data() + 0x14);
    std::uint32_t symbols = load_u32le(in.data() + 0x18);
    if (out_size > kMaxOutput) return fail(Errc::Range, "DSC output too large");

    // The code lengths are stored masked with a LCG keystream seeded by the header.
    std::array<std::uint8_t, kSymbolCount> length{};
    Keystream ks(seed);
    for (std::size_t i = 0; i < kSymbolCount; ++i)
        length[i] = static_cast<std::uint8_t>(in[kLengthsOffset + i] - ks.next());

    // Canonical Huffman: codes ordered by (length, symbol).
    std::array<int, kMaxCodeLength + 1> count{};
    for (auto l : length) {
        if (l > kMaxCodeLength) return fail(Errc::Unsupported, "DSC code length above 32");
        if (l) ++count[l];
    }
    std::array<int, kMaxCodeLength + 2> offset{};
    for (int l = 1; l <= kMaxCodeLength; ++l) offset[l + 1] = offset[l] + count[l];
    std::array<std::uint16_t, kSymbolCount> sorted{};
    {
        auto next = offset;
        for (std::size_t s = 0; s < kSymbolCount; ++s)
            if (length[s]) sorted[next[length[s]]++] = static_cast<std::uint16_t>(s);
    }
    if (offset[kMaxCodeLength + 1] == 0 && symbols > 0) return fail(Errc::Format, "DSC has no codes");

    std::vector<std::uint8_t> out(out_size);
    std::size_t pos = 0;
    BitReader br{in.subspan(kStreamOffset)};

    for (std::uint32_t n = 0; n < symbols; ++n) {
        int code = 0, first = 0, index = 0, sym = -1;
        for (int l = 1; l <= kMaxCodeLength; ++l) {
            code |= static_cast<int>(br.get1());
            int c = count[l];
            if (code - c < first) {
                sym = sorted[index + (code - first)];
                break;
            }
            index += c;
            first = (first + c) << 1;
            code <<= 1;
        }
        if (br.overrun) return fail(Errc::Format, "DSC bit stream truncated");
        if (sym < 0) return fail(Errc::Format, "DSC invalid Huffman code");

        if (sym < 256) {
            if (pos >= out_size) return fail(Errc::Format, "DSC output overflow");
            out[pos++] = static_cast<std::uint8_t>(sym);
        } else {
            std::size_t back = br.get(12) + 2;
            std::size_t len = static_cast<std::size_t>(sym & 255) + 2;
            if (br.overrun) return fail(Errc::Format, "DSC bit stream truncated");
            if (back > pos) return fail(Errc::Format, "DSC back-reference before start");
            if (pos + len > out_size) return fail(Errc::Format, "DSC output overflow");
            for (std::size_t i = 0; i < len; ++i, ++pos) out[pos] = out[pos - back]; // may overlap
        }
    }
    if (pos != out_size) return fail(Errc::Format, "DSC decoded size mismatch");
    return out;
}

} // namespace obgi
