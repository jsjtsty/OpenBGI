#include "bindings/bitmap_effect_subops.hpp"

#include <array>
#include <map>
#include <memory>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

// A tone curve: per channel (R, G, B) the output of every input value.
struct ToneCurve {
    std::array<std::array<std::uint8_t, 256>, 3> lut{};
};

int round_half_away(double v) { return static_cast<int>(v < 0 ? v - 0.5 : v + 0.5); }

// The natural cubic spline through (0, 0), (x1, y1) and (255, 255), sampled at 0..255 and clamped (the original's arithmetic).
void spline3(std::array<std::uint8_t, 256>& out, int x1, int y1) {
    const double h0 = x1, h1 = 255.0 - x1;
    const double c1 = 3.0 * ((255.0 - y1) / h1 - double(y1) / h0) / (2.0 * (h0 + h1));
    for (int i = 0; i < 256; ++i) {
        double x0, y0, y_next, h, ca, cb;
        if (i < x1) x0 = 0, y0 = 0, y_next = y1, h = h0, ca = 0, cb = c1;
        else x0 = x1, y0 = y1, y_next = 255, h = h1, ca = c1, cb = 0;
        const double t = i - x0;
        const double b = (y_next - y0) / h - (2.0 * ca + cb) * h / 3.0;
        const double d = (cb - ca) / (3.0 * h);
        const int v = round_half_away(y0 + t * (b + t * (ca + t * d)));
        out[std::size_t(i)] = static_cast<std::uint8_t>(v <= 0 ? 0 : v >= 256 ? 255 : v);
    }
}

// One pixel (B G R x) through the monochrome mix, the curve and the colour film (the original's 16-bit arithmetic, levels halved).
std::uint32_t tone_pixel(std::uint32_t px, const ToneCurve& c, std::uint32_t mono_colour, int mono_half, std::uint32_t film_colour, int film_half) {
    int ch[3] = {int(px & 0xFF), int((px >> 8) & 0xFF), int((px >> 16) & 0xFF)};
    const int gray16 = ch[0] * 29 + ch[1] * 150 + ch[2] * 77;
    for (int k = 0; k < 3; ++k) {
        const int tint = int((mono_colour >> (8 * k)) & 0xFF);
        const int tinted = (gray16 * tint) >> 16;
        ch[k] += ((tinted - ch[k]) * mono_half) >> 7;
    }
    ch[0] = c.lut[2][std::size_t(ch[0] & 0xFF)];
    ch[1] = c.lut[1][std::size_t(ch[1] & 0xFF)];
    ch[2] = c.lut[0][std::size_t(ch[2] & 0xFF)];
    for (int k = 0; k < 3; ++k) {
        const int film = int((film_colour >> (8 * k)) & 0xFF);
        const int over = ch[k] > 127 ? 255 - (((255 - ch[k]) * (255 - film)) >> 7) : (ch[k] * film) >> 7;
        const int v = ch[k] + (((over - ch[k]) * film_half) >> 7);
        ch[k] = v < 0 ? 0 : v > 255 ? 255 : v;
    }
    return (px & 0xFF000000u) | (std::uint32_t(ch[2]) << 16) | (std::uint32_t(ch[1]) << 8) | std::uint32_t(ch[0]);
}

} // namespace

void register_bitmap_effect_subops(vm::SubopRegistry& r, Display* display) {
    if (!display) return;
    auto curves = std::make_shared<std::map<std::int32_t, ToneCurve>>();
    // 90:CC (id, points): registers tone curve `id` from six words (x, y of the R, G and B curves; x 1..254, y 0..255); a null
    // `points` removes it (an error when there is none). Out-of-range points register nothing, silently, as in the original.
    add(r, 0x90CC, "register tone curve", 2, 0, [curves](Ctx& c) {
        const Word points = c.pop();
        const auto id = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (points == 0) {
            if (curves->erase(id) == 0) c.fail(Errc::Range, "no tone curve " + std::to_string(id));
            return 0;
        }
        std::int32_t p[6];
        for (int i = 0; i < 6; ++i) p[i] = static_cast<std::int32_t>(c.read(points + 4 * Word(i), 4, false));
        if (!c.ok()) return 0;
        for (int k = 0; k < 3; ++k)
            if (p[k * 2] <= 0 || p[k * 2] >= 255 || p[k * 2 + 1] < 0 || p[k * 2 + 1] > 255) return 0;
        ToneCurve& tc = (*curves)[id];
        for (int k = 0; k < 3; ++k) spline3(tc.lut[std::size_t(k)], p[k * 2], p[k * 2 + 1]);
        return 0;
    });
    // 90:CD (dst, src, mono colour, mono level, curve, film colour, effect, film level): dst becomes src through the curve
    add(r, 0x90CD, "apply tone curve", 8, 0, [curves, display](Ctx& c) {
        const auto film_level = static_cast<std::int32_t>(c.pop());
        const auto effect = static_cast<std::int32_t>(c.pop());
        const Word film_colour = c.pop();
        const auto curve = static_cast<std::int32_t>(c.pop());
        const auto mono_level = static_cast<std::int32_t>(c.pop());
        const Word mono_colour = c.pop(), src = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        const SlotInfo* si = display->slot(src);
        if (!si) { c.fail(Errc::Range, "no image slot " + std::to_string(src)); return 0; }
        auto it = curves->find(curve);
        if (it == curves->end()) { c.fail(Errc::Range, "no tone curve " + std::to_string(curve)); return 0; }
        if (si->format != SlotFormat::Bgrx && si->format != SlotFormat::Bgra) { c.fail(Errc::Range, "tone curve needs a 32-bit bitmap"); return 0; }
        if (mono_level < 0 || mono_level > 0x100 || film_level < 0 || film_level > 0x100 || (effect != 8 && effect != 0x26)) {
            c.fail(Errc::Range, "tone curve parameter out of range");
            return 0;
        }
        auto px = display->read_slot(src);
        if (!px) { c.fail(px.error().code, px.error().message); return 0; }
        const bool argb = si->format == SlotFormat::Bgra;
        const SlotFormat format = si->format;
        const std::uint32_t w = si->width, h = si->height;
        for (std::uint32_t& v : *px) {
            if (argb && !(v & 0xFF000000u)) continue;
            v = tone_pixel(v, it->second, mono_colour, mono_level >> 1, film_colour, film_level >> 1);
        }
        if (auto up = display->upload_slot(dst, w, h, format, *px); !up) c.fail(up.error().code, up.error().message);
        return 0;
    });
    // 92:18 (dst, src): dst becomes an 8-bit gray bitmap of src's luminance (times alpha for ARGB)
    add(r, 0x9218, "bitmap to gray", 2, 0, [display](Ctx& c) {
        const Word src = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        const SlotInfo* si = display->slot(src);
        if (!si || (si->format != SlotFormat::Bgrx && si->format != SlotFormat::Bgra)) { c.fail(Errc::Range, "no 32-bit image in slot " + std::to_string(src)); return 0; }
        const bool argb = si->format == SlotFormat::Bgra;
        const std::uint32_t w = si->width, h = si->height;
        auto px = display->read_slot(src);
        if (!px) { c.fail(px.error().code, px.error().message); return 0; }
        for (std::uint32_t& v : *px) {
            int g = int((v >> 8) & 0xFF) * 151 + int((v >> 16) & 0xFF) * 77 + int(v & 0xFF) * 28;
            g = argb ? (g * int(v >> 24)) >> 16 : g >> 8;
            v = std::uint32_t(g);
        }
        if (auto up = display->upload_slot(dst, w, h, SlotFormat::Gray8, *px); !up) c.fail(up.error().code, up.error().message);
        return 0;
    });
    // 92:19 (slot) -> ok: inverts an 8-bit gray bitmap in place; 0 for a missing bitmap or one that is not gray
    add(r, 0x9219, "invert gray bitmap", 1, 1, [display](Ctx& c) {
        const Word slot = c.pop();
        if (!c.ok()) return 0;
        const SlotInfo* si = display->slot(slot);
        if (!si || si->format != SlotFormat::Gray8) { c.push(0); return 0; }
        const std::uint32_t w = si->width, h = si->height;
        auto px = display->read_slot(slot);
        if (!px) { c.fail(px.error().code, px.error().message); return 0; }
        for (std::uint32_t& v : *px) v = 0xFF - (v & 0xFF);
        if (auto up = display->upload_slot(slot, w, h, SlotFormat::Gray8, *px); !up) c.fail(up.error().code, up.error().message);
        else c.push(1);
        return 0;
    });
}

} // namespace obgi
