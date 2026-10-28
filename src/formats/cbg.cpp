#include "formats/cbg.hpp"

#include <array>
#include <cstring>

#include "formats/keystream.hpp"

namespace obgi {

namespace {

constexpr char kMagic[16] = {'C', 'o', 'm', 'p', 'r', 'e', 's', 's', 'e', 'd', 'B', 'G', '_', '_', '_', 0};
constexpr std::size_t kHeaderSize = 0x30;

std::uint16_t load_u16le(const std::uint8_t* p) {
    std::uint16_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

// 7-bit little-endian continuation integer.
bool read_varint(ByteView data, std::size_t& pos, std::uint32_t& out) {
    std::uint32_t n = 0;
    for (unsigned shift = 0; shift <= 28; shift += 7) {
        if (pos >= data.size()) return false;
        std::uint8_t b = data[pos++];
        n |= static_cast<std::uint32_t>(b & 0x7f) << shift;
        if (b < 0x80) {
            out = n;
            return true;
        }
    }
    return false;
}

struct Node {
    std::uint32_t weight = 0;
    bool active = false;
    int child[2] = {-1, -1};
};

} // namespace

bool is_cbg(ByteView in) { return in.size() >= sizeof kMagic && std::memcmp(in.data(), kMagic, sizeof kMagic) == 0; }

std::vector<std::uint8_t> CbgImage::to_rgba() const {
    std::size_t n = static_cast<std::size_t>(width) * height;
    std::vector<std::uint8_t> out(n * 4);
    unsigned ch = channels();
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint8_t* p = &pixels[i * ch];
        std::uint8_t* q = &out[i * 4];
        if (ch == 1) q[0] = q[1] = q[2] = p[0];
        else {
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
        }
        q[3] = ch == 4 ? p[3] : 255;
    }
    return out;
}

Result<CbgImage> cbg_decode(ByteView in) {
    if (!is_cbg(in) || in.size() < kHeaderSize) return fail(Errc::Format, "not a CompressedBG stream");
    const std::uint8_t* h = in.data();
    if (load_u16le(h + 0x2e) != 1) return fail(Errc::Unsupported, "CompressedBG version other than 1");

    CbgImage img;
    img.width = load_u16le(h + 0x10);
    img.height = load_u16le(h + 0x12);
    img.bits_per_pixel = load_u16le(h + 0x14);
    std::uint32_t stream_size = load_u32le(h + 0x20);
    std::uint32_t seed = load_u32le(h + 0x24);
    std::uint32_t table_size = load_u32le(h + 0x28);
    unsigned ch = img.channels();
    if ((ch != 1 && ch != 3 && ch != 4) || img.bits_per_pixel % 8 || !img.width || !img.height)
        return fail(Errc::Format, "CompressedBG invalid dimensions");
    std::size_t raw_size = static_cast<std::size_t>(img.width) * img.height * ch;
    if (raw_size > 100000000) return fail(Errc::Range, "CompressedBG image too large");
    if (table_size > in.size() - kHeaderSize) return fail(Errc::Format, "CompressedBG truncated table");

    // Frequency table: masked with the keystream, guarded by a sum and an xor byte.
    std::vector<std::uint8_t> table(in.begin() + kHeaderSize, in.begin() + kHeaderSize + table_size);
    Keystream ks(seed);
    std::uint8_t sum = 0, xr = 0;
    for (auto& b : table) {
        b = static_cast<std::uint8_t>(b - ks.next());
        sum = static_cast<std::uint8_t>(sum + b);
        xr ^= b;
    }
    if (sum != h[0x2c] || xr != h[0x2d]) return fail(Errc::Format, "CompressedBG table checksum mismatch");

    std::vector<Node> nodes(256);
    std::size_t tpos = 0;
    std::uint64_t total = 0;
    for (auto& n : nodes) {
        if (!read_varint(table, tpos, n.weight)) return fail(Errc::Format, "CompressedBG bad frequency table");
        n.active = n.weight != 0;
        total += n.weight;
    }
    if (!total) return fail(Errc::Format, "CompressedBG empty Huffman tree");
    nodes.reserve(512);

    // Repeatedly merge the two lightest active nodes (ties: lower index); first picked is child 0.
    int root = -1;
    for (;;) {
        int pick[2] = {-1, -1};
        for (int k = 0; k < 2; ++k) {
            int best = -1;
            for (int i = 0; i < static_cast<int>(nodes.size()); ++i)
                if (nodes[i].active && (best < 0 || nodes[i].weight < nodes[best].weight)) best = i;
            if (best < 0) break;
            nodes[best].active = false;
            pick[k] = best;
        }
        if (pick[0] < 0) return fail(Errc::Format, "CompressedBG invalid Huffman tree");
        Node merged;
        merged.active = true;
        merged.weight = nodes[pick[0]].weight + (pick[1] >= 0 ? nodes[pick[1]].weight : 0);
        merged.child[0] = pick[0];
        merged.child[1] = pick[1];
        nodes.push_back(merged);
        root = static_cast<int>(nodes.size()) - 1;
        if (merged.weight == total) break;
    }

    // Huffman stream -> RLE byte stream.
    std::vector<std::uint8_t> stream(stream_size);
    std::size_t bit = (kHeaderSize + table_size) * 8;
    std::size_t bit_end = in.size() * 8;
    for (auto& out : stream) {
        int j = root;
        while (j >= 256) {
            if (bit >= bit_end) return fail(Errc::Format, "CompressedBG truncated bit stream");
            j = nodes[j].child[(in[bit >> 3] >> (7 - (bit & 7))) & 1];
            ++bit;
            if (j < 0) return fail(Errc::Format, "CompressedBG invalid Huffman traversal");
        }
        out = static_cast<std::uint8_t>(j);
    }

    // Alternating literal / zero runs, starting with literal.
    img.pixels.assign(raw_size, 0);
    std::size_t spos = 0, opos = 0;
    bool literal = true;
    while (spos < stream.size()) {
        std::uint32_t len;
        if (!read_varint(stream, spos, len)) return fail(Errc::Format, "CompressedBG bad run length");
        if (len > raw_size - opos) return fail(Errc::Format, "CompressedBG run overflows image");
        if (literal) {
            if (len > stream.size() - spos) return fail(Errc::Format, "CompressedBG truncated literal run");
            std::memcpy(&img.pixels[opos], &stream[spos], len);
            spos += len;
        }
        opos += len;
        literal = !literal;
    }
    if (opos != raw_size) return fail(Errc::Format, "CompressedBG run total mismatch");

    // Undo prediction in place: average of left and above (or whichever exists).
    std::size_t row = static_cast<std::size_t>(img.width) * ch;
    std::uint8_t* px = img.pixels.data();
    for (std::uint32_t y = 0; y < img.height; ++y) {
        for (std::uint32_t x = 0; x < img.width; ++x) {
            std::size_t i = y * row + static_cast<std::size_t>(x) * ch;
            for (unsigned c = 0; c < ch; ++c) {
                std::size_t j = i + c;
                unsigned pred;
                if (x && y) pred = (px[j - ch] + px[j - row]) >> 1;
                else if (x) pred = px[j - ch];
                else if (y) pred = px[j - row];
                else pred = 0;
                px[j] = static_cast<std::uint8_t>(px[j] + pred);
            }
        }
    }
    return img;
}

} // namespace obgi
