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

} // namespace obgi::bgfx
