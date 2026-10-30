#include "render/background_fx.hpp"

#include <algorithm>
#include <cmath>

namespace obgi::bgfx {
namespace {

constexpr double kDeg16ToRad = 2.663161090079238e-07; // 2 pi / (360 << 16)
constexpr double kInv2To32 = 2.3283064365386963e-10;
constexpr double kInv65536 = 1.52587890625e-05;
constexpr double kHalfPi = 1.5707963267948966;

std::uint8_t sat8(int v) { return static_cast<std::uint8_t>(v < 0 ? 0 : v > 255 ? 255 : v); }
std::int32_t ftol(double v) { return std::isfinite(v) && std::fabs(v) < 2147483648.0 ? static_cast<std::int32_t>(v) : INT32_MIN; }
Image blank(std::uint32_t w, std::uint32_t h) { return Image{w, h, std::vector<std::uint32_t>(std::size_t{w} * h, 0)}; }

// blit_rotate.c: SampleXY, the bilinear fetch for 16.16 positions (7 significant fraction bits, neighbours outside read as 0)
std::uint32_t sample_xy(const Image& src, std::uint32_t pos_x, std::uint32_t pos_y) {
    int ix = static_cast<int>(pos_x >> 16), iy = static_cast<int>(pos_y >> 16);
    ix = std::min(ix, 0x7fff), iy = std::min(iy, 0x7fff);
    const unsigned ux = static_cast<unsigned>(ix) & 0xffff, uy = static_cast<unsigned>(iy) & 0xffff;
    const int fx = static_cast<int>((pos_x & 0xfe00) >> 9), fy = static_cast<int>((pos_y & 0xfe00) >> 9);
    const auto at = [&](unsigned x, unsigned y) { return src.px[std::size_t{y} * src.w + x]; };
    std::uint32_t p00 = 0, p01 = 0, p10 = 0, p11 = 0;
    if (uy < src.h) {
        if (ux < src.w) p00 = at(ux, uy);
        if (fx && ux + 1 < src.w) p01 = at(ux + 1, uy);
    }
    if (fy && uy + 1 < src.h) {
        if (ux < src.w) p10 = at(ux, uy + 1);
        if (ux + 1 < src.w) p11 = at(ux + 1, uy + 1);
    }
    int out = 0;
    for (int i = 0; i < 32; i += 8) {
        const int a = (p00 >> i) & 0xff, b = (p01 >> i) & 0xff, c = (p10 >> i) & 0xff, e = (p11 >> i) & 0xff;
        const int h0 = (((a - b) * (128 - fx)) >> 7) + b;
        const int h1 = (((c - e) * (128 - fx)) >> 7) + e;
        out |= sat8((((h0 - h1) * (128 - fy)) >> 7) + h1) << i;
    }
    return static_cast<std::uint32_t>(out);
}

} // namespace

Image crossfade(const Image& first, const Image& second, int level) {
    if (level <= 0) return first;
    if (level >= 0x100) return second;
    Image out = blank(first.w, first.h);
    const int w = level >> 1; // blit.c: Blend32_Plain, the destination's weight
    for (std::size_t i = 0; i < out.px.size(); ++i) {
        std::uint32_t px = 0;
        for (int sh = 0; sh < 32; sh += 8) {
            const int s = (first.px[i] >> sh) & 0xff, d = (second.px[i] >> sh) & 0xff;
            px |= static_cast<std::uint32_t>(sat8(s + (((d - s) * w) >> 7))) << sh;
        }
        out.px[i] = px;
    }
    return out;
}

Image toward_colour(const Image& first, bool white, int level) {
    Image out = blank(first.w, first.h);
    const int p = std::clamp(level, 0, 0x100);
    for (std::size_t i = 0; i < out.px.size(); ++i) {
        std::uint32_t px = first.px[i] & 0xff000000u;
        for (int sh = 0; sh < 24; sh += 8) {
            const int s = (first.px[i] >> sh) & 0xff;
            px |= static_cast<std::uint32_t>(sat8((s * (256 - p) + (white ? 255 * p : 0)) >> 8)) << sh;
        }
        out.px[i] = px;
    }
    return out;
}

Image tiled(const std::array<const Image*, 4>& quads, int ox, int oy) {
    const std::uint32_t w = quads[0]->w, h = quads[0]->h;
    Image out = blank(w, h);
    for (std::uint32_t y = 0; y < h; ++y)
        for (std::uint32_t x = 0; x < w; ++x) {
            const std::int64_t px = std::int64_t{x} + ox, py = std::int64_t{y} + oy;
            if (px < 0 || py < 0 || px >= 2 * std::int64_t{w} || py >= 2 * std::int64_t{h}) continue;
            const Image& q = *quads[(py >= h ? 2 : 0) + (px >= w ? 1 : 0)];
            out.px[std::size_t{y} * w + x] = q.px[std::size_t(py % h) * w + std::size_t(px % w)];
        }
    return out;
}

Image box_blur(const Image& src, int edge, int level) {
    if (level <= 0) return src;
    if (level >= 0x100) level = 0xff;
    Image out = blank(src.w, src.h);
    const int w = static_cast<int>(src.w), n = 2 * level + 1, half = n >> 1;
    const int shift = n < 0x40 ? 0 : (n < 0x80 ? 1 : 2);
    const int q = (0x10003 / n) & 0xffff;
    for (std::uint32_t y = 0; y < src.h; ++y) {
        const std::uint32_t* s = &src.px[std::size_t{y} * src.w];
        int sum[4] = {0, 0, 0, 0};
        const auto add = [&](std::uint32_t p, int sign) {
            for (int c = 0; c < 4; ++c) sum[c] += sign * (static_cast<int>((p >> (c * 8)) & 0xff) >> shift);
        };
        for (int i = -half; i < -half + n; ++i) {
            int idx = i;
            if (edge) idx = idx < 0 ? 0 : (idx >= w ? w - 1 : idx);
            else if (idx < 0 || idx >= w) continue;
            add(s[idx], 1);
        }
        for (int x = 0; x < w; ++x) {
            std::uint32_t px = 0;
            for (int c = 0; c < 4; ++c)
                px |= static_cast<std::uint32_t>(sat8(((static_cast<std::int16_t>(sum[c]) * q) >> 16) << shift)) << (c * 8);
            out.px[std::size_t{y} * src.w + x] = px;
            int idx = x - half; // slide: drop x - half, take x + half + 1
            if (edge || idx >= 0) add(s[idx < 0 ? 0 : idx], -1);
            idx = x + half + 1;
            if (edge || idx < w) add(s[idx >= w ? w - 1 : idx], 1);
        }
    }
    return out;
}

Image scale2(const Image& src, std::uint32_t w, std::uint32_t h, std::int32_t ox, std::int32_t oy, std::int32_t full_w, std::int32_t full_h,
             std::int32_t sx, std::int32_t sy) {
    Image out = blank(w, h);
    if (static_cast<std::uint32_t>(full_w) < 0x20000u || static_cast<std::uint32_t>(full_h) < 0x20000u || sx <= 0 || sy <= 0 || src.px.empty()) return out;
    // Blit_ScaleTo with the destination centre and half the full size
    const std::int32_t cx = static_cast<std::int32_t>(w << 15), cy = static_cast<std::int32_t>(h << 15);
    const std::int32_t hx = static_cast<std::int32_t>(static_cast<std::uint32_t>(full_w) >> 1), hy = static_cast<std::int32_t>(static_cast<std::uint32_t>(full_h) >> 1);
    const double ix = 65536.0 / sx, iy = 65536.0 / sy;
    const double cxs = cx * ix * kInv65536, cys = cy * iy * kInv65536;
    const double hxd = hx * kInv65536, hyd = hy * kInv65536;
    std::int32_t xcorr = 0, ycorr = 0;
    if (static_cast<std::uint32_t>(sx) < 0x10000u) xcorr = ftol((ix * 0.5 - 0.5) * 65536.0);
    if (static_cast<std::uint32_t>(sy) < 0x10000u) ycorr = ftol((iy * 0.5 - 0.5) * 65536.0);
    std::int32_t start_y = ycorr - ftol((hyd - cys) * -65536.0) + oy;
    const std::int32_t start_x = xcorr - ftol((hxd - cxs) * -65536.0) + ox;
    const std::int32_t step_x = ftol(ix * 65536.0), step_y = ftol(iy * 65536.0);
    for (std::uint32_t y = 0; y < h; ++y) {
        std::uint32_t px = static_cast<std::uint32_t>(start_x);
        for (std::uint32_t x = 0; x < w; ++x) {
            out.px[std::size_t{y} * w + x] = sample_xy(src, px, static_cast<std::uint32_t>(start_y));
            px += static_cast<std::uint32_t>(step_x);
        }
        start_y += step_y;
    }
    return out;
}

Image rotate_centred(const Image& src, std::uint32_t w, std::uint32_t h, std::int32_t zoom, std::int32_t angle) {
    Image out = blank(w, h);
    if (zoom <= 0 || src.px.empty()) return out;
    const std::int32_t cx = static_cast<std::int32_t>(src.w << 15), cy = static_cast<std::int32_t>(src.h << 15);
    const double hw = -((w - 1) * 0.5), hh = (h - 1) * 0.5, zd = zoom, zi = 65536.0 / zd;
    const double theta = -(angle * kDeg16ToRad), c = std::cos(theta), s = std::sin(theta);
    const double cy_d = cy * zd * kInv2To32, y0 = cy_d - (s * hw + c * hh);
    const double cx_d = cx * zd * kInv2To32, x0 = (c * hw - s * hh) + cx_d;
    std::int32_t start_y = ftol(y0 * zi * 65536.0), start_x = ftol(x0 * zi * 65536.0);
    const std::int32_t col_y = ftol((s * zi) * -65536.0), col_x = ftol((c * zi) * 65536.0);
    // the step to the next row is perpendicular to the column step: (s, c) * zi. The reference's lines assign these the other way
    // round (rowX from the sine of theta + pi / 2), which would shear an unrotated picture, so the geometry is used instead.
    const std::int32_t row_x = ftol(std::cos(theta + kHalfPi) * zi * -65536.0), row_y = ftol(std::sin(theta + kHalfPi) * zi * 65536.0);
    for (std::uint32_t y = 0; y < h; ++y) {
        std::uint32_t px = static_cast<std::uint32_t>(start_x), py = static_cast<std::uint32_t>(start_y);
        for (std::uint32_t x = 0; x < w; ++x) {
            const auto ipx = static_cast<std::int32_t>(px), ipy = static_cast<std::int32_t>(py);
            if (ipx >= 0 && ipy >= 0 && ipx <= (static_cast<std::int32_t>(src.w) << 16) - 1 && ipy <= (static_cast<std::int32_t>(src.h) << 16) - 1)
                out.px[std::size_t{y} * w + x] = src.px[std::size_t(ipy >> 16) * src.w + std::size_t(ipx >> 16)];
            px += static_cast<std::uint32_t>(col_x), py += static_cast<std::uint32_t>(col_y);
        }
        start_x += row_x, start_y += row_y;
    }
    return out;
}

namespace {
// the dim of blit.c as the filters use it: src * (0x100 - level) >> 8 per colour byte, top byte 0
std::uint32_t dim(std::uint32_t sv, int level) {
    const int inv = 0x100 - level;
    return static_cast<std::uint32_t>(((sv & 0xff) * inv) >> 8) | static_cast<std::uint32_t>((((sv >> 8) & 0xff) * inv) >> 8) << 8 |
           static_cast<std::uint32_t>((((sv >> 16) & 0xff) * inv) >> 8) << 16;
}
} // namespace

void filter_kind(std::vector<std::uint32_t>& px, int kind, std::uint32_t colour, int level) {
    level = std::clamp(level, 0, 0x100);
    if (level == 0) return;
    switch (kind) {
    case 0:
        if (colour == 0) {
            for (auto& v : px) v = dim(v, level);
            break;
        }
        { // Tint32: (s * inv >> 8) + (colour * level >> 8) per colour byte, saturated, top byte 0
            const int inv = 0x100 - level;
            const int add[3] = {static_cast<int>(((colour & 0xff) * unsigned(level)) >> 8), static_cast<int>((((colour >> 8) & 0xff) * unsigned(level)) >> 8),
                                static_cast<int>((((colour >> 16) & 0xff) * unsigned(level)) >> 8)};
            for (auto& v : px) {
                std::uint32_t out = 0;
                for (int c = 0; c < 3; ++c) out |= static_cast<std::uint32_t>(sat8(static_cast<int>((((v >> (8 * c)) & 0xff) * unsigned(inv)) >> 8) + add[c])) << (8 * c);
                v = out;
            }
        }
        break;
    case 1:
        if (colour == 0) {
            for (auto& v : px) v = dim(v, level);
            break;
        }
        {
            std::uint32_t add = 0;
            for (int c = 0; c < 3; ++c) add |= static_cast<std::uint32_t>(sat8(static_cast<int>((((colour >> (8 * c)) & 0xff) * unsigned(level)) >> 8))) << (8 * c);
            for (auto& v : px) {
                std::uint32_t out = 0;
                for (int sh = 0; sh < 32; sh += 8) out |= static_cast<std::uint32_t>(sat8(static_cast<int>((v >> sh) & 0xff) + static_cast<int>((add >> sh) & 0xff))) << sh;
                v = out;
            }
        }
        break;
    case 2: { // the monochrome tint: luma picks an entry of a table, added to the dimmed picture
        const int inv = 0x100 - level;
        int table[0x80][3];
        for (int i = 0; i < 0x80; ++i) {
            int v = (i + 1) * level;
            if (v == 0x8000) v = 0x7fff;
            for (int c = 0; c < 3; ++c) table[i][c] = (((v * static_cast<int>((colour >> (c * 8)) & 0xff)) >> 16) << 1) & 0xffff;
        }
        for (auto& p : px) {
            const int b = p & 0xff, g = (p >> 8) & 0xff, r = (p >> 16) & 0xff, luma = (b * 28 + g * 151 + r * 77) >> 9;
            p = static_cast<std::uint32_t>(sat8(((b * inv) >> 8) + table[luma][0])) | static_cast<std::uint32_t>(sat8(((g * inv) >> 8) + table[luma][1])) << 8 |
                static_cast<std::uint32_t>(sat8(((r * inv) >> 8) + table[luma][2])) << 16;
        }
        break;
    }
    case 3: { // s + ((s ^ colour) - s) * (level >> 1) >> 7 per colour byte; the top byte stays
        const int l2 = level >> 1;
        for (auto& p : px) {
            std::uint32_t out = p & 0xff000000u;
            for (int sh = 0; sh < 24; sh += 8) {
                const int sc = (p >> sh) & 0xff, cc = (colour >> sh) & 0xff;
                out |= static_cast<std::uint32_t>(sat8(((((sc ^ cc) - sc) * l2) >> 7) + sc)) << sh;
            }
            p = out;
        }
        break;
    }
    default: break;
    }
}

void filter_by_gray(std::vector<std::uint32_t>& px, std::uint32_t colour, const std::vector<std::uint32_t>& gray, int param, int level) {
    level = std::clamp(level, 0, 0x100);
    const int base = 0x100 - ((1 << param) + 1) * level;
    for (std::size_t i = 0; i < px.size() && i < gray.size(); ++i) {
        const int v = (static_cast<int>(gray[i] & 0xff) << param) + base;
        if (v <= 0) { px[i] = colour; continue; }
        if (v >= 0x100) continue;
        const int w = v >> 1;
        std::uint32_t out = 0;
        for (int sh = 0; sh < 24; sh += 8) {
            const int cc = (colour >> sh) & 0xff, dc = (px[i] >> sh) & 0xff;
            out |= static_cast<std::uint32_t>(sat8((((dc - cc) * w) >> 7) + cc)) << sh;
        }
        px[i] = out;
    }
}

Image crossfade_to(const Image& a, const Image& b, int ratio, bool has_alpha) {
    Image out = blank(a.w, a.h);
    const int inv = 0x100 - ratio;
    for (std::size_t i = 0; i < out.px.size(); ++i) {
        const std::uint32_t av = a.px[i], bv = b.px[i];
        if (!has_alpha) { // CrossfadeTo32: a + ((b - a) * ratio >> 8) on all four bytes
            std::uint32_t px = 0;
            for (int sh = 0; sh < 32; sh += 8) {
                const int ac = (av >> sh) & 0xff, bc = (bv >> sh) & 0xff;
                px |= static_cast<std::uint32_t>(sat8(ac + (((bc - ac) * ratio) >> 8))) << sh;
            }
            out.px[i] = px;
            continue;
        }
        // CrossfadeTo32A: weights by the alphas; the result alpha is the sum of the weights >> 8 (a 16-bit lane)
        const unsigned wa = (av >> 24) * static_cast<unsigned>(inv), wb = (bv >> 24) * static_cast<unsigned>(ratio);
        const unsigned sum = (wa + wb) & 0xffffu;
        if (sum == 0) continue;
        const int w = static_cast<int>((wa << 7) / sum);
        std::uint32_t px = ((sum >> 8) & 0xff) << 24;
        for (int sh = 0; sh < 24; sh += 8) {
            const int ac = (av >> sh) & 0xff, bc = (bv >> sh) & 0xff;
            px |= static_cast<std::uint32_t>(sat8(bc + (((ac - bc) * w) >> 7))) << sh;
        }
        out.px[i] = px;
    }
    return out;
}

Transformed transform(const Image& src, std::int32_t ox16, std::int32_t oy16, std::int32_t angle, std::int32_t sx, std::int32_t sy, bool smooth) {
    Transformed t;
    if (src.px.empty() || sx <= 0 || sy <= 0) return t;
    const double W = src.w, H = src.h, cx = ox16 * kInv65536, cy = oy16 * kInv65536;
    const double theta = angle * kDeg16ToRad, c = std::cos(theta), s = std::sin(theta), SX = sx * kInv65536, SY = sy * kInv65536;
    // the four corners relative to the origin, y pointing up (the 0.0001 keeps a corner on a pixel boundary from adding a pixel)
    const double X[4] = {-cx, W - cx - 0.0001, -cx, W - cx - 0.0001}, Y[4] = {cy, cy, cy - H + 0.0001, cy - H + 0.0001};
    double min_x = 1e9, max_x = -1e9, min_y = 1e9, max_y = -1e9;
    for (int i = 0; i < 4; ++i) {
        const double xs = SX * X[i], ys = SY * Y[i], rx = xs * c - ys * s, ry = ys * c + xs * s;
        min_x = std::min(min_x, rx), max_x = std::max(max_x, rx), min_y = std::min(min_y, ry), max_y = std::max(max_y, ry);
    }
    const std::int32_t x0 = static_cast<std::int32_t>(std::floor(min_x)), x1 = static_cast<std::int32_t>(std::floor(max_x));
    const std::int32_t y1 = static_cast<std::int32_t>(std::ceil(max_y)), y0 = static_cast<std::int32_t>(std::ceil(min_y));
    const std::int32_t out_w = x1 - x0 + 1, out_h = y1 - y0 + 1;
    if (out_w < 2 || out_h < 2 || std::int64_t{out_w} * out_h > 64'000'000) return t;
    t.anchor_x = -x0, t.anchor_y = y1;
    t.image = blank(static_cast<std::uint32_t>(out_w), static_cast<std::uint32_t>(out_h));
    for (std::int32_t py = 0; py < out_h; ++py)
        for (std::int32_t px = 0; px < out_w; ++px) {
            const double rx = px + 0.5 - t.anchor_x, ry = t.anchor_y - (py + 0.5); // relative to the origin, y up
            const double xs = rx * c + ry * s, ys = -rx * s + ry * c;              // undo the rotation
            const double u = cx + xs / SX, v = cy - ys / SY;                       // the picture's pixel coordinates
            if (u < 0 || v < 0 || u >= W || v >= H) continue;
            std::uint32_t& dst = t.image.px[std::size_t(py) * out_w + px];
            if (smooth) dst = sample_xy(src, static_cast<std::uint32_t>(std::max(0.0, (u - 0.5) * 65536.0)), static_cast<std::uint32_t>(std::max(0.0, (v - 0.5) * 65536.0)));
            else dst = src.px[std::size_t(v) * src.w + std::size_t(u)];
        }
    return t;
}

std::int32_t ease(std::int32_t t, int curve) {
    constexpr double deg16_to_rad = 2.663161090079238e-07;
    constexpr double powers[6] = {2.0, 2.5, 3.0, 4.0, 5.0, 6.0};
    switch (curve) {
    case 1: return static_cast<std::int32_t>((std::cos((0xB40000 - std::int64_t{t} * 180 / 256) * deg16_to_rad) + 1.0) * 32768.0);
    case 2: return static_cast<std::int32_t>(std::sin((std::int64_t{t} * 90 / 256) * deg16_to_rad) * 65536.0);
    case 3: return static_cast<std::int32_t>((1.0 - std::sin((0x5A0000 - std::int64_t{t} * 90 / 256) * deg16_to_rad)) * 65536.0);
    default:
        if (curve >= 4 && curve <= 15) {
            const double n = powers[(curve - 4) / 2];
            if (curve & 1) return static_cast<std::int32_t>((1.0 - std::pow(double(0x1000000 - t), n) / std::pow(16777216.0, n)) * 65536.0);
            return static_cast<std::int32_t>(std::pow(double(t), n) * 65536.0 / std::pow(16777216.0, n));
        }
        return t / 256;
    }
}

void mosaic(Image& dst, const Image& src, int amount, int extra) {
    if (amount <= 0) { // Blit_Blend: a copy at transparency 0, nothing at 0x100 and above, else the plain blend
        if (extra <= 0) dst = src;
        else if (extra < 0x100) dst = crossfade(src, dst, extra);
        return;
    }
    if (extra >= 0x100) return;
    const int n = amount + 1, level = std::max(extra, 0);
    std::vector<std::uint32_t> tmp(dst.w);
    for (std::uint32_t y = 0; y < dst.h; y += static_cast<std::uint32_t>(n)) {
        const std::uint32_t* s = &src.px[std::size_t{y} * src.w];
        const std::uint32_t rows = std::min<std::uint32_t>(dst.h - y, static_cast<std::uint32_t>(n));
        for (std::uint32_t x = 0; x < dst.w; x += static_cast<std::uint32_t>(n)) {
            const std::uint32_t cols = std::min<std::uint32_t>(dst.w - x, static_cast<std::uint32_t>(n));
            for (std::uint32_t k = 0; k < cols; ++k) tmp[x + k] = s[x];
        }
        for (std::uint32_t r = 0; r < rows; ++r) {
            std::uint32_t* d = &dst.px[std::size_t{y + r} * dst.w];
            for (std::uint32_t x = 0; x < dst.w; ++x) {
                if (!level) { d[x] = tmp[x]; continue; }
                std::uint32_t out = 0;
                for (int i = 0; i < 32; i += 8) {
                    const int dc = (d[x] >> i) & 0xff, mc = (tmp[x] >> i) & 0xff;
                    out |= static_cast<std::uint32_t>(sat8((((dc - mc) * level) >> 8) + mc)) << i;
                }
                d[x] = out;
            }
        }
    }
}

void xform_copy(Image& dst, std::int32_t x16, std::int32_t y16, const Image& src, std::int32_t cx16, std::int32_t cy16, std::int32_t angle,
                std::int32_t sx, std::int32_t sy, int level, bool smooth) {
    std::fill(dst.px.begin(), dst.px.end(), 0u);
    if (level >= 0x100 || sx <= 0 || sy <= 0) return;
    const Transformed t = transform(src, cx16, cy16, angle, sx, sy, smooth);
    const std::int32_t ox = (x16 >> 16) - t.anchor_x, oy = (y16 >> 16) - t.anchor_y, inv = 0x100 - std::max(level, 0);
    for (std::int32_t y = 0; y < static_cast<std::int32_t>(t.image.h); ++y)
        for (std::int32_t x = 0; x < static_cast<std::int32_t>(t.image.w); ++x) {
            const std::int32_t dx = ox + x, dy = oy + y;
            if (dx < 0 || dy < 0 || dx >= static_cast<std::int32_t>(dst.w) || dy >= static_cast<std::int32_t>(dst.h)) continue;
            std::uint32_t v = t.image.px[std::size_t(y) * t.image.w + std::size_t(x)];
            if (inv != 0x100)
                v = (v & 0xff000000u) | (((v & 0xff) * inv) >> 8) | ((((v >> 8) & 0xff) * inv) >> 8) << 8 | ((((v >> 16) & 0xff) * inv) >> 8) << 16;
            dst.px[std::size_t(dy) * dst.w + std::size_t(dx)] = v;
        }
}

Image colour_mode(const Image& src, int mode, std::uint32_t colour, int level) {
    Image out = src;
    if (mode == 0) return out;
    const int inv = 0x100 - level;
    if (mode == 1) { // Blit_FilterKind3: s + ((s ^ colour) - s) * (level >> 1) >> 7, the top byte kept
        const int l2 = level >> 1;
        for (auto& v : out.px) {
            std::uint32_t r = v & 0xff000000u;
            for (int i = 0; i < 24; i += 8) {
                const int sc = (v >> i) & 0xff, cc = (colour >> i) & 0xff;
                r |= static_cast<std::uint32_t>(sat8(((((sc ^ cc) - sc) * l2) >> 7) + sc)) << i;
            }
            v = r;
        }
    } else if (mode == 2) { // Blit_FilterKind2: the monochrome tint, the top byte becomes 0
        int table[0x80][3];
        for (int i = 0; i < 0x80; ++i) {
            int v = (i + 1) * level;
            if (v == 0x8000) v = 0x7fff;
            for (int c = 0; c < 3; ++c) table[i][c] = (((v * static_cast<int>((colour >> (c * 8)) & 0xff)) >> 16) << 1) & 0xffff;
        }
        for (auto& p : out.px) {
            const int b = p & 0xff, g = (p >> 8) & 0xff, r = (p >> 16) & 0xff, luma = (b * 28 + g * 151 + r * 77) >> 9;
            p = static_cast<std::uint32_t>(sat8(((b * inv) >> 8) + table[luma][0])) | static_cast<std::uint32_t>(sat8(((g * inv) >> 8) + table[luma][1])) << 8 |
                static_cast<std::uint32_t>(sat8(((r * inv) >> 8) + table[luma][2])) << 16;
        }
    } else { // 3 Blit_TintKeepAlpha: s * inv >> 8 plus the colour * level >> 8; 4 Blit_AddColour: s plus the colour * level >> 8; the alpha kept
        int add[3];
        for (int i = 0; i < 3; ++i) add[i] = static_cast<int>(((colour >> (8 * i)) & 0xff) * static_cast<unsigned>(level)) >> 8;
        for (auto& v : out.px) {
            std::uint32_t r = v & 0xff000000u;
            for (int i = 0; i < 3; ++i) {
                const int sc = (v >> (8 * i)) & 0xff;
                r |= static_cast<std::uint32_t>(sat8((mode == 3 ? static_cast<int>((static_cast<unsigned>(sc) * static_cast<unsigned>(inv)) >> 8) : sc) + add[i])) << (8 * i);
            }
            v = r;
        }
    }
    return out;
}

void gather_add(Image& dst, const Image& src, std::uint32_t cx, std::uint32_t cy, std::uint32_t level) {
    std::uint32_t table[128];
    for (std::uint32_t i = 0; i < 128; ++i) table[i] = ((128 - i) * (256 - level)) >> 8;
    const std::uint32_t x0 = static_cast<std::uint32_t>(src.w - 1) * cx >> 1, y0 = static_cast<std::uint32_t>(src.h - 1) * cy >> 1;
    const std::uint32_t step_x = 0x10000 - cx, step_y = 0x10000 - cy;
    std::uint32_t y = y0;
    for (std::uint32_t i = 0; i < src.h; ++i, y += step_y) {
        std::uint32_t x = x0;
        for (std::uint32_t j = 0; j < src.w; ++j, x += step_x) {
            const std::int32_t xi = static_cast<std::int32_t>(x) >> 16, yi = static_cast<std::int32_t>(y) >> 16;
            const std::uint32_t fx = (x & 0xfe00) >> 9, fy = (y & 0xfe00) >> 9; // the top 7 fraction bits
            const std::uint32_t w00 = ((128 - fx) * (128 - fy)) >> 7, w10 = (fx * (128 - fy)) >> 7;
            const std::uint32_t w01 = ((128 - fx) * fy) >> 7, w11 = (fx * fy) >> 7;
            std::uint32_t idx[4];
            // the packing quirk of the original (PACKSSDW): a row pair saturates when its right-hand share is not zero
            if (w10) idx[0] = 0x81, idx[1] = 0;
            else idx[0] = 0x80 - w00, idx[1] = 0x80;
            if (w11) idx[2] = 0x81, idx[3] = 0;
            else idx[2] = 0x80 - w01, idx[3] = 0x80;
            const std::uint32_t pix = src.px[std::size_t{i} * src.w + j];
            for (std::uint32_t k = 0; k < 4; ++k) {
                const std::int32_t px = xi + static_cast<std::int32_t>(k & 1), py = yi + static_cast<std::int32_t>(k >> 1);
                if (idx[k] >= 0x80 || px < 0 || py < 0 || px >= static_cast<std::int32_t>(dst.w) || py >= static_cast<std::int32_t>(dst.h)) continue;
                const std::uint32_t wgt = table[idx[k]];
                std::uint32_t& d = dst.px[std::size_t(py) * dst.w + std::size_t(px)];
                std::uint32_t o = 0;
                for (std::uint32_t c = 0; c < 32; c += 8) {
                    std::uint32_t v = (((pix >> c) & 0xff) * wgt) >> 7;
                    v += (d >> c) & 0xff;
                    o |= std::min<std::uint32_t>(v, 0xff) << c;
                }
                d = o;
            }
        }
    }
}

void apply_gray(Image& dst, const std::vector<std::uint32_t>& gray, std::uint32_t gw, std::uint32_t gh, int x, int y) {
    for (std::uint32_t py = 0; py < dst.h; ++py)
        for (std::uint32_t px = 0; px < dst.w; ++px) {
            const std::int64_t gx = std::int64_t{px} + x, gy = std::int64_t{py} + y;
            if (gx < 0 || gy < 0 || gx >= gw || gy >= gh) continue;
            const std::uint32_t mask = gray[std::size_t(gy) * gw + std::size_t(gx)] & 0xff;
            std::uint32_t& v = dst.px[std::size_t{py} * dst.w + px];
            v = (v & 0xffffffu) | ((((v >> 24) * mask) << 16) & 0xff000000u);
        }
}

} // namespace obgi::bgfx
