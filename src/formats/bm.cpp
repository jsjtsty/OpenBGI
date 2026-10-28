#include "formats/bm.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace obgi {

namespace {

constexpr char kMagic[16] = {'B', 'F', '_', 'M', 'o', 'v', 'i', 'e', '_', '_', '_', '_', '_', '_', '_', 0};
constexpr std::size_t kFrameTable = 0xc0;
constexpr int kZigzag[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                             12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                             35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                             58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

bool read_varint(ByteView d, std::size_t& pos, std::uint32_t& out) {
    std::uint32_t v = 0;
    for (unsigned shift = 0; shift < 35; shift += 7) {
        if (pos >= d.size()) return false;
        std::uint8_t c = d[pos++];
        v |= static_cast<std::uint32_t>(c & 0x7f) << (shift & 31);
        if (c < 0x80) {
            out = v;
            return true;
        }
    }
    return false;
}

// Huffman tree built exactly like the original (FUN_004b15a0), including its selection quirk:
// the second scan of each merge starts at index t+1, so some ties resolve unlike a textbook heap.
struct Tree {
    struct Node {
        bool active = false;
        std::uint32_t freq = 0;
        int child[2] = {-1, -1};
    };
    std::vector<Node> nodes;
    int root = -1;
    int leaves = 0;

    bool build(const std::vector<std::uint32_t>& freq) {
        leaves = static_cast<int>(freq.size());
        const int total_nodes = 2 * leaves - 1;
        nodes.assign(total_nodes, {});
        std::uint64_t total = 0;
        for (int i = 0; i < leaves; ++i) {
            nodes[i].freq = freq[i];
            if (freq[i]) {
                nodes[i].active = true;
                total += freq[i];
            }
        }
        for (int count = leaves; count < total_nodes; ++count) {
            int pick[2] = {-1, -1};
            std::uint64_t sum = 0;
            for (int t = 0; t < 2; ++t) {
                int c = -1;
                for (int i = 0; i < count; ++i)
                    if (nodes[i].active) {
                        c = i;
                        break;
                    }
                if (c >= 0)
                    for (int j = t + 1; j < count; ++j)
                        if (nodes[j].active && nodes[j].freq < nodes[c].freq) c = j;
                pick[t] = c;
                if (c < 0) break;
                nodes[c].active = false;
                sum += nodes[c].freq;
            }
            nodes[count].active = true;
            nodes[count].freq = static_cast<std::uint32_t>(sum);
            nodes[count].child[0] = pick[0];
            nodes[count].child[1] = pick[1];
            if (total <= sum) {
                root = count;
                return true;
            }
        }
        return false;
    }
};

// MSB-first bit reader that records overruns instead of throwing.
struct Bits {
    ByteView d;
    std::size_t pos;
    unsigned used = 0;
    bool overrun = false;

    Bits(ByteView data, std::size_t start) : d(data), pos(start) {}
    unsigned bit() {
        if (pos >= d.size()) {
            overrun = true;
            return 0;
        }
        unsigned v = (d[pos] >> (7 - used)) & 1u;
        if (++used == 8) {
            used = 0;
            ++pos;
        }
        return v;
    }
    unsigned bits(int n) {
        unsigned v = 0;
        while (n-- > 0) v = (v << 1) | bit();
        return v;
    }
    int symbol(const Tree& t) {
        int x = t.root;
        while (x >= t.leaves) {
            x = t.nodes[x].child[bit()];
            if (x < 0 || overrun) {
                overrun = true;
                return 0;
            }
        }
        return x;
    }
    std::size_t end() const { return pos + (used ? 1 : 0); }
};

int sign_extend(unsigned v, int n) {
    int r = static_cast<int>(v);
    if (n && !((v >> (n - 1)) & 1)) r = (r | (-1 * (1 << n))) + 1;
    return r;
}

bool read_freqs(ByteView fr, std::size_t& pos, std::size_t n, std::vector<std::uint32_t>& out) {
    out.resize(n);
    for (auto& f : out)
        if (!read_varint(fr, pos, f)) return false;
    return true;
}

// Entropy-decode `count` coefficients (a multiple of 64) of one block row.
bool decode_coeffs(ByteView fr, std::size_t pos, std::size_t count, const Tree& dct, const Tree& act,
                   std::vector<std::int16_t>& co) {
    co.assign(count, 0);
    Bits r(fr, pos);
    std::uint16_t prev = 0;
    for (std::size_t blk = 0; blk < count; blk += 64) {
        int cat = r.symbol(dct);
        int v = cat ? sign_extend(r.bits(cat), cat) : 0;
        prev = static_cast<std::uint16_t>(prev + v);
        co[blk] = static_cast<std::int16_t>(prev);
    }
    if (r.overrun) return false;
    Bits a(fr, r.end());
    for (std::size_t blk = 0; blk < count; blk += 64) {
        int k = 1;
        while (k < 64) {
            int s = a.symbol(act);
            if (a.overrun) return false;
            if (s == 0) break;
            if (s == 0xf) {
                k += 16;
                continue;
            }
            k += s & 15;
            int size = (s >> 4) & 15;
            int v = size ? sign_extend(a.bits(size), size) : 0;
            if (k < 64) co[blk + kZigzag[k]] = static_cast<std::int16_t>(v);
            ++k;
        }
    }
    return !a.overrun;
}

// One 8-point AAN pass (float32, same operation order as the original; note outputs 3 and 4).
void aan8(const float* in, int stride, float* out, int ostride) {
    const float c1 = static_cast<float>(1.4142135381698608), c2 = static_cast<float>(1.8477590084075928),
                c3 = static_cast<float>(1.0823922157287598), c4 = static_cast<float>(-2.613126039505005);
    float t0 = in[0], t1 = in[stride], t2 = in[2 * stride], t3 = in[3 * stride], t4 = in[4 * stride],
          t5 = in[5 * stride], t6 = in[6 * stride], t7 = in[7 * stride];
    float tmp10 = t0 + t4, tmp11 = t0 - t4, tmp13 = t2 + t6;
    float tmp12 = (t2 - t6) * c1 - tmp13;
    float a0 = tmp10 + tmp13, a3 = tmp10 - tmp13, a1 = tmp11 + tmp12, a2 = tmp11 - tmp12;
    float z13 = t5 + t3, z10 = t5 - t3, z11 = t1 + t7, z12 = t1 - t7;
    float b7 = z11 + z13, tm11 = (z11 - z13) * c1;
    float z5 = (z10 + z12) * c2;
    float tm10 = z12 * c3 - z5, tm12 = z10 * c4 + z5;
    float b6 = tm12 - b7, b5 = tm11 - b6, b4 = tm10 + b5;
    out[0] = a0 + b7;
    out[ostride] = a1 + b6;
    out[2 * ostride] = a2 + b5;
    out[3 * ostride] = a3 - b4;
    out[4 * ostride] = a3 + b4;
    out[5 * ostride] = a2 - b5;
    out[6 * ostride] = a1 - b6;
    out[7 * ostride] = a0 - b7;
}

void idct_plane(const std::int16_t* co, const float* q, float out[64]) {
    float x[64], y[64];
    for (int i = 0; i < 64; ++i) x[i] = static_cast<float>(co[i]) * q[i];
    for (int col = 0; col < 8; ++col) aan8(x + col, 8, y + col, 8);   // vertical
    for (int row = 0; row < 8; ++row) aan8(y + row * 8, 1, x + row * 8, 1); // horizontal
    for (int i = 0; i < 64; ++i) {
        int v = static_cast<int>(x[i]) >> 3; // truncating conversion, arithmetic shift
        out[i] = static_cast<float>(std::clamp(v + 128, 0, 255));
    }
}

std::uint8_t clamp_byte(float v) { return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 255.0f)); }

} // namespace

Result<BmDecoder> BmDecoder::open(ByteView file) {
    if (file.size() < kFrameTable || std::memcmp(file.data(), kMagic, sizeof kMagic) != 0)
        return fail(Errc::Format, "not a BF_Movie stream");
    BmDecoder d;
    d.file_ = file;
    const std::uint8_t* p = file.data();
    d.version_ = load_u32le(p + 0x10);
    d.width_ = load_u32le(p + 0x14);
    d.height_ = load_u32le(p + 0x18);
    d.bits_ = load_u32le(p + 0x1c);
    d.rate_ = load_u32le(p + 0x24);
    d.frame_count_ = load_u32le(p + 0x28);
    if (d.version_ != 0x10000 && d.version_ != 0x10001) return fail(Errc::Unsupported, "BF_Movie version");
    if (!d.width_ || !d.height_ || d.width_ > 16384 || d.height_ > 16384)
        return fail(Errc::Range, "BF_Movie dimensions");
    if (d.bits_ != 24 && d.bits_ != 32) return fail(Errc::Unsupported, "BF_Movie bit depth");
    if (static_cast<std::uint64_t>(d.frame_count_) * 4 > file.size() - kFrameTable)
        return fail(Errc::Format, "BF_Movie frame table beyond file");

    static constexpr double s[8] = {1.0, 1.387039845, 1.306562965, 1.175875602,
                                    1.0, 0.785694958, 0.541196100, 0.275899379};
    for (int t = 0; t < 2; ++t)
        for (int v = 0; v < 8; ++v)
            for (int u = 0; u < 8; ++u) {
                float scale = static_cast<float>(s[v] * s[u]);
                d.quant_[t][v * 8 + u] = static_cast<float>(p[0x40 + 0x40 * t + v * 8 + u]) * scale;
            }
    d.canvas_.assign(static_cast<std::size_t>(d.width_) * d.height_ * 4, 0);
    return d;
}

Result<void> BmDecoder::decode_frame(std::uint32_t index) {
    if (index >= frame_count_) return fail(Errc::Range, "BF_Movie frame index");
    std::uint32_t frame_off = load_u32le(file_.data() + kFrameTable + 4 * index);
    if (frame_off >= file_.size()) return fail(Errc::Format, "BF_Movie frame offset");
    ByteView fr = file_.subspan(frame_off);

    const std::uint32_t w8 = (width_ + 7) / 8, h8 = (height_ + 7) / 8, mask_bytes = (w8 + 7) / 8;
    std::size_t pos = 0;
    std::vector<std::uint32_t> f1, f2;
    if (!read_freqs(fr, pos, 16, f1) || !read_freqs(fr, pos, 176, f2))
        return fail(Errc::Format, "BF_Movie truncated frequency tables");
    Tree dct, act;
    if (!dct.build(f1) || !act.build(f2)) return fail(Errc::Format, "BF_Movie invalid Huffman table");
    if (static_cast<std::uint64_t>(h8 + 1) * 4 > fr.size() - pos) return fail(Errc::Format, "BF_Movie row table");
    std::vector<std::uint32_t> offs(h8 + 1);
    for (std::uint32_t k = 0; k <= h8; ++k) offs[k] = load_u32le(fr.data() + pos + 4 * k);

    std::vector<std::int16_t> co;
    std::vector<std::uint32_t> present;
    for (std::uint32_t r = 0; r < h8; ++r) {
        std::size_t p = offs[r];
        if (p + mask_bytes > fr.size()) return fail(Errc::Format, "BF_Movie block row offset");
        const std::uint8_t* mask = fr.data() + p;
        std::size_t q = p + mask_bytes;
        std::uint32_t cnt;
        if (!read_varint(fr, q, cnt)) return fail(Errc::Format, "BF_Movie coefficient count");
        if (!cnt) continue;
        present.clear();
        for (std::uint32_t b = 0; b < w8; ++b)
            if ((mask[b >> 3] >> (b & 7)) & 1) present.push_back(b);
        const std::size_t n = present.size();
        if (cnt != n * 192) return fail(Errc::Format, "BF_Movie coefficient count mismatch");
        if (!decode_coeffs(fr, q, cnt, dct, act, co)) return fail(Errc::Format, "BF_Movie truncated coefficients");

        for (std::size_t j = 0; j < n; ++j) {
            float Y[64], Cb[64], Cr[64];
            idct_plane(&co[j * 64], quant_[0], Y);
            idct_plane(&co[(n + j) * 64], quant_[1], Cb);
            idct_plane(&co[(2 * n + j) * 64], quant_[1], Cr);
            const std::uint32_t x0 = present[j] * 8, y0 = r * 8;
            const std::uint32_t bw = std::min<std::uint32_t>(8, width_ - x0), bh = std::min<std::uint32_t>(8, height_ - y0);
            for (std::uint32_t yy = 0; yy < bh; ++yy)
                for (std::uint32_t xx = 0; xx < bw; ++xx) {
                    int i = static_cast<int>(yy * 8 + xx);
                    float cb = Cb[i] - 128, cr = Cr[i] - 128;
                    float R = std::floor(Y[i] + static_cast<float>(1.402) * cr + static_cast<float>(0.5));
                    float G = std::floor(Y[i] - static_cast<float>(0.34414) * cb - static_cast<float>(0.71414) * cr +
                                         static_cast<float>(0.5));
                    float B = std::floor(Y[i] + static_cast<float>(1.772) * cb + static_cast<float>(0.5));
                    std::uint8_t* px = &canvas_[((y0 + yy) * static_cast<std::size_t>(width_) + x0 + xx) * 4];
                    px[0] = clamp_byte(B);
                    px[1] = clamp_byte(G);
                    px[2] = clamp_byte(R);
                }
        }
    }
    if (bits_ != 32) return {};

    // Alpha plane.
    std::size_t ao = offs[h8];
    std::uint32_t mode = 1;
    if (version_ == 0x10001) {
        if (ao + 4 > fr.size()) return fail(Errc::Format, "BF_Movie alpha header");
        mode = load_u32le(fr.data() + ao);
        ao += 4;
    }
    std::uint8_t* a = canvas_.data();
    const std::size_t end = canvas_.size();
    if (mode == 1) {
        const std::ptrdiff_t stride = static_cast<std::ptrdiff_t>(width_) * 4;
        std::size_t dst = 3, flags = ao, j = ao + 1;
        unsigned bit = 0;
        while (dst < end) {
            if (flags >= fr.size()) return fail(Errc::Format, "BF_Movie alpha stream truncated");
            if (!((fr[flags] >> bit) & 1)) {
                if (j >= fr.size()) return fail(Errc::Format, "BF_Movie alpha stream truncated");
                a[dst] = fr[j++];
                dst += 4;
            } else {
                if (j + 2 > fr.size()) return fail(Errc::Format, "BF_Movie alpha stream truncated");
                unsigned t = fr[j] | (fr[j + 1] << 8);
                j += 2;
                int dx = t & 0x3f;
                if (dx > 31) dx -= 64;
                int dy = (t >> 6) & 7;
                if (dy) dy -= 8;
                unsigned len = (t >> 9) + 3;
                for (unsigned k = 0; k < len && dst < end; ++k, dst += 4) {
                    std::ptrdiff_t src = static_cast<std::ptrdiff_t>(dst) + dy * stride + dx * 4;
                    if (src < 3 || static_cast<std::size_t>(src) >= end)
                        return fail(Errc::Format, "BF_Movie alpha copy out of range");
                    a[dst] = a[src];
                }
            }
            if (++bit > 7) {
                bit = 0;
                flags = j;
                ++j;
            }
        }
    } else if (mode == 2) {
        if (ao + 4 > fr.size()) return fail(Errc::Format, "BF_Movie alpha header");
        std::uint32_t size = load_u32le(fr.data() + ao);
        std::size_t p = ao + 4;
        std::vector<std::uint32_t> fq;
        if (size > (1u << 28) || !read_freqs(fr, p, 256, fq)) return fail(Errc::Format, "BF_Movie alpha table");
        Tree tree;
        if (!tree.build(fq)) return fail(Errc::Format, "BF_Movie invalid alpha Huffman table");
        Bits rd(fr, p);
        std::vector<std::uint8_t> buf(size);
        for (auto& b : buf) {
            b = static_cast<std::uint8_t>(rd.symbol(tree));
            if (rd.overrun) return fail(Errc::Format, "BF_Movie alpha stream truncated");
        }
        std::size_t off = (static_cast<std::size_t>(w8) * h8 + 7) / 8, bi = 0;
        if (off > buf.size()) return fail(Errc::Format, "BF_Movie alpha block map");
        for (std::uint32_t by = 0; by < h8; ++by)
            for (std::uint32_t bx = 0; bx < w8; ++bx, ++bi) {
                if (!((buf[bi >> 3] >> (bi & 7)) & 1)) continue;
                std::uint32_t bw = std::min<std::uint32_t>(8, width_ - bx * 8), bh = std::min<std::uint32_t>(8, height_ - by * 8);
                if (off + static_cast<std::size_t>(bw) * bh > buf.size())
                    return fail(Errc::Format, "BF_Movie alpha block data truncated");
                for (std::uint32_t yy = 0; yy < bh; ++yy)
                    for (std::uint32_t xx = 0; xx < bw; ++xx)
                        a[((by * 8 + yy) * static_cast<std::size_t>(width_) + bx * 8 + xx) * 4 + 3] = buf[off++];
            }
    } else {
        return fail(Errc::Unsupported, "BF_Movie alpha mode");
    }
    return {};
}

} // namespace obgi
