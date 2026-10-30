#include "render/vector_maps.hpp"

#include <algorithm>
#include <cmath>

namespace obgi::vecmap {

namespace {

constexpr int kNotVectorMap = static_cast<int>(0x80000003);
constexpr double kTwoPi = 6.283185307179586;

// MSVC _ftol as the reference's BGI_Ftol: truncation, and the x87 "integer indefinite" for NaN and out-of-range values
std::int32_t ftol(double d) {
    if (!(d > -2147483649.0 && d < 2147483648.0)) return static_cast<std::int32_t>(0x80000000u);
    return static_cast<std::int32_t>(d);
}

std::int16_t lo16(std::uint32_t v) { return static_cast<std::int16_t>(v & 0xFFFF); }
std::int16_t hi16(std::uint32_t v) { return static_cast<std::int16_t>(v >> 16); }
std::uint32_t pack(std::int32_t dx, std::int32_t dy) { return static_cast<std::uint16_t>(dx) | static_cast<std::uint32_t>(static_cast<std::uint16_t>(dy)) << 16; }

bool is_vector(const Map& b) { return b.mode == Mode::Vector && b.w != 0 && b.h != 0; }
std::uint32_t& vec_at(Map& b, std::uint32_t x, std::uint32_t y) { return b.px[std::size_t{y} * b.w + x]; }

std::int16_t sat16(std::int32_t v) { return static_cast<std::int16_t>(v >= 0x8000 ? 0x7FFF : v < -0x8000 ? -0x8000 : v); }

std::int32_t wrap_sub(std::int32_t a, std::int32_t b) { return static_cast<std::int32_t>(static_cast<std::uint32_t>(a) - static_cast<std::uint32_t>(b)); }

} // namespace

// ---- generators (vecmaps.c) ----

int stretch_range(Map& b, int ox, int oy, int rw, int rh) {
    if (!is_vector(b)) return kNotVectorMap;
    if (static_cast<std::uint32_t>(rw) == 0 || static_cast<std::uint32_t>(rh) == 0) return static_cast<int>(0x80000005);
    std::uint32_t y_acc = 0;
    for (std::uint32_t y = 0; y < b.h; ++y, y_acc += static_cast<std::uint32_t>(rh)) {
        const int v = static_cast<int>((y_acc << 4) / b.h) + ((oy - static_cast<int>(y)) << 4);
        std::uint32_t x_acc = 0;
        for (std::uint32_t x = 0; x < b.w; ++x, x_acc += static_cast<std::uint32_t>(rw)) {
            const int u = static_cast<int>((x_acc << 4) / b.w) + ((ox - static_cast<int>(x)) << 4);
            vec_at(b, x, y) = pack(u, v);
        }
    }
    return 0;
}

int random(Map& b, int amplitude, const Rand15& rand) {
    if (!is_vector(b)) return kNotVectorMap;
    const std::uint32_t range = static_cast<std::uint32_t>(2 * amplitude + 1);
    const auto draw = [&] {
        const std::uint32_t hi = rand() << 15;
        const std::uint32_t r = hi | rand();
        return static_cast<std::int32_t>(amplitude - static_cast<int>(range ? r % range : r));
    };
    for (std::uint32_t y = 0; y < b.h; ++y)
        for (std::uint32_t x = 0; x < b.w; ++x) {
            const std::int32_t dx = draw();
            const std::int32_t dy = draw();
            vec_at(b, x, y) = pack(dx, dy);
        }
    return 0;
}

namespace {

// one component: (hi^2 - lo^2) / (2 * scale), saturated to 15 bits, negative when the first sample is the higher one
std::int16_t height_slope(std::uint32_t a, std::uint32_t b, std::int32_t scale) {
    if (a == b) return 0;
    const std::uint32_t big = std::max(a, b), small = std::min(a, b);
    const std::uint32_t div = static_cast<std::uint32_t>(2 * scale);
    const std::uint64_t num = std::uint64_t{big - small} * std::uint64_t{static_cast<std::uint32_t>(big + small)};
    const std::uint64_t q = div ? num / div : num;
    const std::int32_t r = q >= 0x8000u ? 0x7FFF : static_cast<std::int32_t>(q);
    return static_cast<std::int16_t>(a > b ? -r : r);
}

} // namespace

int from_height(Map& vec, const Map& height, std::int32_t scale) {
    if (!is_vector(vec)) return kNotVectorMap;
    if (height.mode != Mode::Height || height.w != vec.w + 1 || height.h != vec.h + 1) return static_cast<int>(0x80000004);
    if (static_cast<std::uint32_t>(scale) == 0) scale = 1;
    for (std::uint32_t y = 0; y < vec.h; ++y) {
        const std::uint32_t* h0 = &height.px[std::size_t{y} * height.w];
        const std::uint32_t* h1 = h0 + height.w;
        for (std::uint32_t x = 0; x < vec.w; ++x) vec_at(vec, x, y) = pack(height_slope(h0[x], h0[x + 1], scale), height_slope(h0[x], h1[x], scale));
    }
    return 0;
}

int ripple(Map& b, int cx, int cy, int period, int phase, int amplitude) {
    if (!is_vector(b)) return kNotVectorMap;
    if (static_cast<std::uint32_t>(period) == 0) return static_cast<int>(0x80000006);
    Map hgt(Mode::Height, b.w + 1, b.h + 1);
    for (std::uint32_t y = 0; y < hgt.h; ++y) {
        const int dy = cy - static_cast<int>(y);
        for (std::uint32_t x = 0; x < hgt.w; ++x) {
            const int dx = cx - static_cast<int>(x);
            const double dist = std::sqrt(static_cast<double>(static_cast<std::uint32_t>(dx * dx + dy * dy)));
            hgt.px[std::size_t{y} * hgt.w + x] = static_cast<std::uint32_t>(ftol((1.0 - std::cos((dist - phase) * kTwoPi / period)) * amplitude));
        }
    }
    return from_height(b, hgt, 0x10000);
}

int polar(Map& b, int cx, int cy, std::int32_t angle16, int k) {
    if (!is_vector(b)) return kNotVectorMap;
    const double rad = static_cast<double>(angle16) * 2.663161090079238e-07; // 16.16 degrees to radians
    const double s = std::sin(rad), c = std::cos(rad);
    const double kk = c == 0.0 ? 2147483647.0 : static_cast<double>(b.h) / c;
    for (std::uint32_t y = 0; y < b.h; ++y) {
        const int dy = static_cast<int>(y) - cy;
        for (std::uint32_t x = 0; x < b.w; ++x) {
            const int dx = static_cast<int>(x) - cx;
            if (c == 0.0) {
                vec_at(b, x, y) = 0;
                continue;
            }
            const double dist = std::sqrt(static_cast<double>(dy * dy + dx * dx));
            const double kd = k;
            const std::int32_t q = ftol(((kd + kk) * dist) * 16.0 / (dist * s + kd));
            const double deg = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) * 57.29577951308232;
            const std::int32_t u = ftol(static_cast<double>(16 * (static_cast<int>(b.w) - 1)) * (1.0 - (180.0 - deg) * 0.002777777777777778));
            vec_at(b, x, y) = pack(sat16(wrap_sub(wrap_sub(u, 16 * dx), 16 * cx)), sat16(wrap_sub(wrap_sub(q, 16 * dy), 16 * cy)));
        }
    }
    return 0;
}

int bend_angle(Map& b, int cx, int cy, std::int32_t angle16, int radius) {
    if (!is_vector(b)) return kNotVectorMap;
    if (static_cast<std::uint32_t>(angle16) == 0 || static_cast<std::uint32_t>(radius) == 0) return static_cast<int>(0x80000008);
    const double rad = static_cast<double>(angle16) * 2.663161090079238e-07;
    const double sn = std::sin(rad), cs = std::cos(rad);
    for (std::uint32_t y = 0; y < b.h; ++y) {
        const int dy = static_cast<int>(y) - cy;
        for (std::uint32_t x = 0; x < b.w; ++x) {
            const int dx = static_cast<int>(x) - cx;
            const double dist = std::sqrt(static_cast<double>(dy * dy + dx * dx));
            const double r = radius;
            if (dist != 0.0 && dist < r) {
                const double s = dist * sn / r;
                const double c = std::sqrt(1.0 - s * s);
                const double t = (c - cs) * (s / c);
                const double theta = std::atan2(static_cast<double>(std::abs(dy)), static_cast<double>(std::abs(dx)));
                vec_at(b, x, y) = pack(static_cast<std::int16_t>(ftol(std::cos(theta) * (16 * (cx - static_cast<int>(x))) * t)),
                                       static_cast<std::int16_t>(ftol(std::sin(theta) * (16 * (cy - static_cast<int>(y))) * t)));
            } else {
                vec_at(b, x, y) = 0;
            }
        }
    }
    return 0;
}

int bend(Map& b, int cx, int cy, int radius, int height) {
    if (!is_vector(b)) return kNotVectorMap;
    if (static_cast<std::uint32_t>(radius) == 0 || static_cast<std::uint32_t>(height) == 0) return static_cast<int>(0x80000008);
    Map hgt(Mode::Height, b.w + 1, b.h + 1);
    for (std::uint32_t y = 0; y < hgt.h; ++y) {
        const int dy = static_cast<int>(y) - cy;
        for (std::uint32_t x = 0; x < hgt.w; ++x) {
            const int dx = static_cast<int>(x) - cx;
            const double hd = height;
            const double dist = std::sqrt(static_cast<double>(dx * dx + dy * dy));
            hgt.px[std::size_t{y} * hgt.w + x] =
                dist < hd ? static_cast<std::uint32_t>(ftol(4194304.0 - 4194304.0 * std::sin(dist * 3.141592653589793 / static_cast<double>(2 * height)))) : 0;
        }
    }
    return from_height(b, hgt, static_cast<std::int32_t>(0x40000000u / static_cast<std::uint32_t>(radius)));
}

int sine(Map& b, int periodX, int phaseX, int ampX, int periodY, int phaseY, int ampY) {
    if (!is_vector(b)) return kNotVectorMap;
    if (periodX == 0) periodX = 1, ampX = 0;
    if (periodY == 0) periodY = 1, ampY = 0;
    std::vector<std::int16_t> row_dx(b.h), col_dy(b.w);
    for (std::uint32_t y = 0; y < b.h; ++y)
        row_dx[y] = static_cast<std::int16_t>(ftol(std::sin((static_cast<double>(y) + phaseY) * (1.0 / periodY) * kTwoPi) * ampY * 16.0));
    for (std::uint32_t x = 0; x < b.w; ++x)
        col_dy[x] = static_cast<std::int16_t>(ftol(std::sin((static_cast<double>(x) + phaseX) * (1.0 / periodX) * kTwoPi) * ampX * 16.0));
    for (std::uint32_t y = 0; y < b.h; ++y)
        for (std::uint32_t x = 0; x < b.w; ++x) vec_at(b, x, y) = pack(row_dx[y], col_dy[x]);
    return 0;
}

int radial(Map& b, int type, int cx, int cy, int range) {
    if (b.mode != Mode::VecDist || b.w == 0 || b.h == 0) return kNotVectorMap;
    if (static_cast<std::uint32_t>(type) >= 2) return static_cast<int>(0x80000009);
    const std::uint32_t divisor = static_cast<std::uint32_t>(range) > 0 ? static_cast<std::uint32_t>(range) * 4 : 0xFFFFFFFFu;
    for (std::uint32_t y = 0; y < b.h; ++y) {
        const int dy = static_cast<int>(y) - cy;
        for (std::uint32_t x = 0; x < b.w; ++x) {
            const int dx = static_cast<int>(x) - cx;
            const double dist = std::sqrt(static_cast<double>(dy * dy + dx * dx));
            const double inv = 1.0 / dist;
            const double tx = static_cast<double>(0x7FFF * dx), ty = static_cast<double>(0x7FFF * dy);
            const auto a = static_cast<std::int16_t>(ftol((type == 0 ? tx : ty) * inv));
            const auto c = static_cast<std::int16_t>(ftol(inv * (type == 0 ? ty : tx)));
            const auto ring = static_cast<std::uint16_t>(static_cast<std::uint32_t>(ftol(dist * 4.0)) % divisor);
            const std::size_t i = (std::size_t{y} * b.w + x) * 2;
            b.px[i] = pack(a, c);
            b.px[i + 1] = ring;
        }
    }
    return 0;
}

int linear(Map& b, int type) {
    static constexpr bool horizontal[4] = {true, false, true, false};
    static constexpr bool use_y[4] = {true, false, false, true};
    if (b.mode != Mode::VecDist || b.w == 0 || b.h == 0) return kNotVectorMap;
    if (static_cast<std::uint32_t>(type) >= 4) return static_cast<int>(0x8000000A);
    for (std::uint32_t y = 0; y < b.h; ++y)
        for (std::uint32_t x = 0; x < b.w; ++x) {
            const std::size_t i = (std::size_t{y} * b.w + x) * 2;
            b.px[i] = pack(horizontal[type] ? 0x7FFF : 0, horizontal[type] ? 0 : 0x7FFF);
            b.px[i + 1] = static_cast<std::uint16_t>((use_y[type] ? y : x) << 2);
        }
    return 0;
}

// ---- displacement (blit_displace.c) ----

namespace {

int sat8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

// the source as the reference's blitter addresses it: a pixel at byte offsets (row, column), 0 outside; the row offset is checked
// against the whole bitmap, the column offset against the row
std::uint32_t fetch(const bgfx::Image& src, int y, int dy, int col) {
    const int pitch = static_cast<int>(src.w) * 4;
    const int row_off = y * pitch + dy * pitch, col_off = col * 4;
    if (static_cast<unsigned>(col_off) >= static_cast<unsigned>(pitch) || static_cast<unsigned>(row_off) >= static_cast<unsigned>(pitch * static_cast<int>(src.h))) return 0;
    return src.px[static_cast<std::size_t>(row_off / pitch) * src.w + static_cast<std::size_t>(col_off / 4)];
}

void displace_int(bgfx::Image& dst, const bgfx::Image& src, const Map& vec, int level) {
    for (std::uint32_t y = 0; y < src.h; ++y)
        for (std::uint32_t x = 0; x < src.w; ++x) {
            const std::uint32_t v = vec.px[std::size_t{y} * vec.w + x];
            const int dy = ((hi16(v) * level) + 0x800) >> 12, dx = ((lo16(v) * level) + 0x800) >> 12;
            dst.px[std::size_t{y} * src.w + x] = fetch(src, static_cast<int>(y), dy, static_cast<int>(x) + dx);
        }
}

void displace_int2(bgfx::Image& dst, const bgfx::Image& src, const Map& vec1, const Map& vec2, int level) {
    const int big_l = level * 64;
    for (std::uint32_t y = 0; y < src.h; ++y)
        for (std::uint32_t x = 0; x < src.w; ++x) {
            const std::size_t i = std::size_t{y} * src.w + x;
            const std::uint32_t v1 = vec1.px[i], v2 = vec2.px[i];
            const int a1x = static_cast<std::int16_t>(lo16(v1) & 0xFFFC), a1y = static_cast<std::int16_t>(hi16(v1) & 0xFFFC);
            const int tx = static_cast<std::int16_t>(lo16(v2) - a1x), ty = static_cast<std::int16_t>(hi16(v2) - a1y);
            int dx = static_cast<std::int16_t>((tx * big_l) >> 16) + (a1x >> 2) + 2;
            int dy = static_cast<std::int16_t>((ty * big_l) >> 16) + (a1y >> 2) + 2;
            dx = static_cast<std::int16_t>(dx) >> 2;
            dy = static_cast<std::int16_t>(dy) >> 2;
            dst.px[i] = fetch(src, static_cast<int>(y), dy, static_cast<int>(x) + dx);
        }
}

// bilinear sample; v holds the displacement in 13.3 fixed point per 16-bit half; the 2 x 2 neighbourhood follows fetch()'s bounds rules
std::uint32_t sample_bilinear(const bgfx::Image& src, int y, int x, std::uint32_t v) {
    const int pitch = static_cast<int>(src.w) * 4, total = pitch * static_cast<int>(src.h);
    const int dx = lo16(v), dy = hi16(v);
    const int fx = dx & 7, fy = dy & 7;
    const int col_off = (x + (dx >> 3)) * 4;
    const int r0 = y * pitch + (dy >> 3) * pitch;
    const auto at = [&](int row, int col) { return src.px[static_cast<std::size_t>(row / pitch) * src.w + static_cast<std::size_t>(col / 4)]; };
    std::uint32_t p00 = 0, p01 = 0, p10 = 0, p11 = 0;
    if (static_cast<unsigned>(r0) < static_cast<unsigned>(total)) {
        if (static_cast<unsigned>(col_off) < static_cast<unsigned>(pitch)) p00 = at(r0, col_off);
        if (static_cast<unsigned>(col_off + 4) < static_cast<unsigned>(pitch)) p01 = at(r0, col_off + 4);
    }
    if (fy) {
        const int r1 = r0 + pitch;
        if (static_cast<unsigned>(r1) < static_cast<unsigned>(total)) {
            if (static_cast<unsigned>(col_off) < static_cast<unsigned>(pitch)) p10 = at(r1, col_off);
            if (static_cast<unsigned>(col_off + 4) < static_cast<unsigned>(pitch)) p11 = at(r1, col_off + 4);
        }
    }
    int out = 0;
    for (int i = 0; i < 32; i += 8) {
        const int a = (p00 >> i) & 0xFF, b = (p01 >> i) & 0xFF, c = (p10 >> i) & 0xFF, e = (p11 >> i) & 0xFF;
        const int h0 = (((a - b) * (8 - fx)) >> 3) + b;
        const int h1 = (((c - e) * (8 - fx)) >> 3) + e;
        out |= sat8((((h0 - h1) * (8 - fy)) >> 3) + h1) << i;
    }
    return static_cast<std::uint32_t>(out);
}

std::uint32_t half_pair(int x, int y) { return pack(x, y); }

void displace_lin(bgfx::Image& dst, const bgfx::Image& src, const Map& vec) {
    for (std::uint32_t y = 0; y < src.h; ++y)
        for (std::uint32_t x = 0; x < src.w; ++x) {
            const std::uint32_t v = vec.px[std::size_t{y} * vec.w + x];
            dst.px[std::size_t{y} * src.w + x] = sample_bilinear(src, static_cast<int>(y), static_cast<int>(x), half_pair(lo16(v) >> 1, hi16(v) >> 1));
        }
}

void displace_lin_level(bgfx::Image& dst, const bgfx::Image& src, const Map& vec, int level) {
    const int big_l = level << 7;
    for (std::uint32_t y = 0; y < src.h; ++y)
        for (std::uint32_t x = 0; x < src.w; ++x) {
            const std::uint32_t v = vec.px[std::size_t{y} * vec.w + x];
            dst.px[std::size_t{y} * src.w + x] =
                sample_bilinear(src, static_cast<int>(y), static_cast<int>(x), half_pair((lo16(v) * big_l) >> 16, (hi16(v) * big_l) >> 16));
        }
}

void displace_lin2(bgfx::Image& dst, const bgfx::Image& src, const Map& vec1, const Map& vec2, int level) {
    const int big_l = level << 7;
    for (std::uint32_t y = 0; y < src.h; ++y)
        for (std::uint32_t x = 0; x < src.w; ++x) {
            const std::size_t i = std::size_t{y} * src.w + x;
            const std::uint32_t v1 = vec1.px[i], v2 = vec2.px[i];
            const int a1x = static_cast<std::int16_t>(lo16(v1) & 0xFFFE), a1y = static_cast<std::int16_t>(hi16(v1) & 0xFFFE);
            const int tx = static_cast<std::int16_t>(lo16(v2) - a1x), ty = static_cast<std::int16_t>(hi16(v2) - a1y);
            const int hx = static_cast<std::int16_t>(((tx * big_l) >> 16) + (a1x >> 1));
            const int hy = static_cast<std::int16_t>(((ty * big_l) >> 16) + (a1y >> 1));
            dst.px[i] = sample_bilinear(src, static_cast<int>(y), static_cast<int>(x), half_pair(hx, hy));
        }
}

} // namespace

int displace(bgfx::Image& dst, const bgfx::Image& src, const Map& vec1, const Map* vec2, int level, int amount) {
    if (static_cast<unsigned>(level) > 0x100u) return 3;
    if (vec1.mode != Mode::Vector || vec1.w != src.w || vec1.h != src.h) return 0xC;
    if (vec2 && (vec2->mode != Mode::Vector || vec2->w != vec1.w || vec2->h != vec1.h)) return 0xD;
    dst.w = src.w, dst.h = src.h;
    dst.px.assign(src.px.size(), 0);
    if (vec2) {
        if (amount == 0) displace_int2(dst, src, vec1, *vec2, level);
        else if (level == 0) displace_lin(dst, src, vec1);
        else if (level == 0x100) displace_lin(dst, src, *vec2);
        else displace_lin2(dst, src, vec1, *vec2, level);
    } else if (amount != 0) {
        if (level == 0) dst.px = src.px;
        else if (level == 0x100) displace_lin(dst, src, vec1);
        else displace_lin_level(dst, src, vec1, level);
    } else {
        if (level == 0) dst.px = src.px;
        else displace_int(dst, src, vec1, level);
    }
    return 0;
}

// ---- ripple definitions (bmpmgr.c) ----

namespace {

// one period of sin() scaled by amp (attenuated by 2^-k when k > 0) as (x = y) int16 pairs, in the original's order of operations
std::size_t ripple_cycle(std::vector<std::int16_t>& table, std::size_t at, int period, double amp, int k) {
    const double scale = k > 0 ? 1.0 / static_cast<double>(1 << k) : 0.0;
    for (int i = 0; i < period; ++i) {
        double v = std::sin(static_cast<double>(i) * kTwoPi / period) * amp;
        if (k > 0) v *= scale;
        const auto s = static_cast<std::int16_t>(ftol(v));
        table[at++] = s;
        table[at++] = s;
    }
    return at;
}

void reset(RippleDef& d, int period, int total) {
    d = {};
    d.defined = true;
    d.period = period;
    d.total = total;
    d.table.assign(static_cast<std::size_t>(total) * 2, 0);
}

} // namespace

int define_ripple(RippleDefs& m, int no, int period, int amplitude, int count, int rings) {
    if (static_cast<std::uint32_t>(no) >= 8u) return 0x10;
    if (period == 0) period = 1;
    period *= 4;
    if (count == 0) count = 1;
    if (rings == 0) rings = 1;
    if (period < 0 || count < 0 || rings < 0) return 0x10;
    RippleDef& d = m[static_cast<std::size_t>(no)];
    reset(d, period * count, period * count * rings);
    std::size_t p = 0;
    for (int r = 0; r < rings; ++r) {
        p = ripple_cycle(d.table, p, period, static_cast<double>(static_cast<std::uint32_t>(amplitude)), 0);
        p += static_cast<std::size_t>(count - 1) * static_cast<std::size_t>(period) * 2; // the silent cycles stay zero
    }
    return 0;
}

int define_ripple_ex(RippleDefs& m, int no, int period, int amplitude, int fade_in, int fade_out, int count, int rings) {
    if (static_cast<std::uint32_t>(no) >= 8u) return 0x10;
    if (period == 0) period = 1;
    period *= 4;
    if (count == 0) count = 1;
    if (rings == 0) rings = 1;
    if (period < 0 || count < 0 || rings < 0 || fade_in < 0 || fade_out < 0 || fade_in > 30 || fade_out > 30) return 0x10;
    const int cycle = (fade_in + fade_out + 1) * period;
    RippleDef& d = m[static_cast<std::size_t>(no)];
    reset(d, cycle * count, cycle * count * rings);
    const double amp = static_cast<double>(static_cast<std::uint32_t>(amplitude));
    std::size_t p = 0;
    for (int r = 0; r < rings; ++r) {
        p += static_cast<std::size_t>(count - 1) * static_cast<std::size_t>(cycle) * 2; // the leading silent cycles
        for (int k = fade_in; k >= 1; --k) p = ripple_cycle(d.table, p, period, amp, k);
        p = ripple_cycle(d.table, p, period, amp, 0);
        for (int k = 1; k <= fade_out; ++k) p = ripple_cycle(d.table, p, period, amp, k);
    }
    return 0;
}

namespace {

// start index for `selector` rings before the running position, wrapped into the definition's period
int ripple_index(const RippleDef& d, int selector) {
    int idx = d.pos - selector * 4;
    if (idx < d.base && d.period > 0) {
        const std::uint32_t back = static_cast<std::uint32_t>(d.base - idx) % static_cast<std::uint32_t>(d.period);
        idx = static_cast<int>(static_cast<std::uint32_t>(d.period) - back) + d.base;
    }
    return idx;
}

} // namespace

int ripple_check(const RippleDefs& m, bool& ok, int no, int selector, int rings) {
    if (static_cast<std::uint32_t>(no) >= 8u) return 0x10;
    const RippleDef& d = m[static_cast<std::size_t>(no)];
    if (!d.defined) return 0x11;
    const int idx = ripple_index(d, selector);
    ok = idx >= 0 && !((d.total - idx) < rings * 4);
    return 0;
}

int ripple_fill(const RippleDefs& m, std::vector<std::uint32_t>& table, int no, int selector, int level, int rings) {
    bool ok = false;
    if (const int r = ripple_check(m, ok, no, selector, rings); r != 0) return r;
    if (!ok) return 0x12;
    const RippleDef& d = m[static_cast<std::size_t>(no)];
    const int idx = ripple_index(d, selector);
    table.assign(static_cast<std::size_t>(rings) * 4, 0);
    for (int i = 0; i < rings * 4; ++i) {
        const int v = d.table[static_cast<std::size_t>(idx + i) * 2];
        const std::uint32_t w = static_cast<std::uint32_t>((v * level) >> 8) & 0xFFFFu;
        table[static_cast<std::size_t>(i)] = w << 16 | w;
    }
    return 0;
}

// ---- ripple blitters (blit_ripple.c) ----

namespace {

// the pixel offset (from the pixel itself) of a VecDist pixel through the ring table
long ripple_offset(const Map& v, std::size_t i, const std::vector<std::uint32_t>& table, std::uint32_t width) {
    const int vx = lo16(v.px[i * 2]), vy = hi16(v.px[i * 2]);
    const std::uint32_t ring = v.px[i * 2 + 1] & 0xFFFF;
    const std::uint32_t t = ring < table.size() ? table[ring] : 0;
    const int ax = lo16(t), ay = hi16(t);
    return static_cast<long>((vx * ax) >> 16) + static_cast<long>((vy * ay) >> 16) * static_cast<long>(width);
}

} // namespace

int ripple_blit(bgfx::Image& dst, const bgfx::Image& src, const Map& vecdist, const std::vector<std::uint32_t>& table) {
    if (vecdist.mode != Mode::VecDist || vecdist.w != src.w || vecdist.h != src.h) return 0xC;
    dst.w = src.w, dst.h = src.h;
    dst.px.assign(src.px.size(), 0);
    const long size = static_cast<long>(src.px.size());
    for (std::uint32_t y = 0; y < src.h; ++y)
        for (std::uint32_t x = 0; x < src.w; ++x) {
            const std::size_t i = std::size_t{y} * src.w + x;
            const long at = static_cast<long>(i) + ripple_offset(vecdist, i, table, src.w);
            dst.px[i] = at >= 0 && at < size ? src.px[static_cast<std::size_t>(at)] : 0;
        }
    return 0;
}

int ripple_blend(bgfx::Image& dst, const bgfx::Image& src, bool src_has_alpha, const Map& vecdist, const std::vector<std::uint32_t>& table, int level) {
    if (vecdist.mode != Mode::VecDist || vecdist.w != src.w || vecdist.h != src.h) return 0xC;
    if (dst.w != src.w || dst.h != src.h) return 0xF;
    const long size = static_cast<long>(src.px.size());
    const int inv = 0x100 - level;
    for (std::uint32_t y = 0; y < src.h; ++y)
        for (std::uint32_t x = 0; x < src.w; ++x) {
            const std::size_t i = std::size_t{y} * src.w + x;
            long at = static_cast<long>(i) + ripple_offset(vecdist, i, table, src.w);
            at %= size;
            if (at < 0) at += size;
            const std::uint32_t sv = src.px[static_cast<std::size_t>(at)], dv = dst.px[i];
            if (!src_has_alpha) {
                int out = static_cast<int>(sv & 0xFF000000u);
                for (int s = 0; s < 24; s += 8) {
                    const int sc = (sv >> s) & 0xFF, dc = (dv >> s) & 0xFF;
                    out |= sat8((((dc - sc) * level) >> 8) + sc) << s;
                }
                dst.px[i] = static_cast<std::uint32_t>(out);
            } else {
                const int a = static_cast<int>(sv >> 24);
                if (!a) continue;
                const int w = (a * inv) >> 8;
                int out = static_cast<int>(dv & 0xFF000000u);
                for (int s = 0; s < 24; s += 8) {
                    const int sc = (sv >> s) & 0xFF, dc = (dv >> s) & 0xFF;
                    out |= sat8((((sc - dc) * w) >> 8) + dc) << s;
                }
                dst.px[i] = static_cast<std::uint32_t>(out);
            }
        }
    return 0;
}

} // namespace obgi::vecmap
