#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <vector>

#include "render/background_fx.hpp"

// The vector-map bitmaps of the reference engine (gfx/vecmaps.c, blit_displace.c, blit_ripple.c, bmpmgr.c's ripple definitions). They
// are bitmap modes the GPU slots do not have, so Display keeps them in a table of their own, keyed by the same slot numbers.
//   Vector  (mode 4): one word per pixel, low half dx, high half dy, both 12.4 fixed point: the displacement added before sampling.
//   Height  (mode 5): one word per pixel, the unsigned height field the generators differentiate.
//   VecDist (mode 6): two words per pixel, {vx | vy << 16, ring}: a direction (unit vector scaled to 0x7FFF) and a distance in quarter
//                     pixels, which the ripple ring tables index.
namespace obgi::vecmap {

enum class Mode : std::uint8_t { Vector = 4, Height = 5, VecDist = 6 };

struct Map {
    Mode mode = Mode::Vector;
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint32_t> px; // w * h words, or w * h * 2 for VecDist

    Map() = default;
    Map(Mode m, std::uint32_t width, std::uint32_t height)
        : mode(m), w(width), h(height), px(std::size_t{width} * height * (m == Mode::VecDist ? 2 : 1), 0) {}
};

// Result codes of the generators are the instructions' own: 0 ok, 0x80000003 the map is not an empty-free map of the right mode, then
// the generator's own range codes (0x80000004 height size, 5 empty range, 6 zero period, 8 zero angle/radius, 9 / 0xA bad type).
using Rand15 = std::function<std::uint32_t()>; // 0 .. 0x7FFF

int stretch_range(Map& b, int ox, int oy, int rw, int rh);                     // 91:10
int random(Map& b, int amplitude, const Rand15& rand);                         // 91:11
int ripple(Map& b, int cx, int cy, int period, int phase, int amplitude);      // 91:12
int polar(Map& b, int cx, int cy, std::int32_t angle16, int k);                // 91:13
int bend_angle(Map& b, int cx, int cy, std::int32_t angle16, int radius);      // 91:14
int bend(Map& b, int cx, int cy, int radius, int height);                      // 91:15
int sine(Map& b, int periodX, int phaseX, int ampX, int periodY, int phaseY, int ampY); // 91:16
int from_height(Map& vec, const Map& height, std::int32_t scale);
int radial(Map& b, int type, int cx, int cy, int range);                       // 92:10
int linear(Map& b, int type);                                                  // 92:11

// blit_displace.c: Blit_Displace. `src` is read through vec1 (or the mix of vec1 and vec2, which may be null, by level / 256) into a
// result of src's size. 0 ok, 3 bad level, 0xC / 0xD vec1 / vec2 is not a vector map of src's size.
int displace(bgfx::Image& dst, const bgfx::Image& src, const Map& vec1, const Map* vec2, int level, int amount);

// bmpmgr.c: the eight ripple definitions ("92 00" / "92 01"), each a table of (x = y) int16 pairs.
struct RippleDef {
    bool defined = false;
    int pos = 0, base = 0, period = 0, total = 0;
    std::vector<std::int16_t> table; // total pairs, x half then y half
};
using RippleDefs = std::array<RippleDef, 8>;

int define_ripple(RippleDefs& m, int no, int period, int amplitude, int count, int rings);
int define_ripple_ex(RippleDefs& m, int no, int period, int amplitude, int fade_in, int fade_out, int count, int rings);
// 0 ok, 0x10 bad number, 0x11 not defined; *ok says whether `rings` rings are left from the phase `selector`.
int ripple_check(const RippleDefs& m, bool& ok, int no, int selector, int rings);
// The ring table (rings * 4 entries of {ax, ay}) the ripple blitters read; the ripple_check codes, 0x12 when too few rings are left.
int ripple_fill(const RippleDefs& m, std::vector<std::uint32_t>& table, int no, int selector, int level, int rings);

// blit_ripple.c: `src` read through the VecDist map and the ring table into a result of src's size, 0 where the sample leaves the
// bitmap. 0xC when the map's size differs.
int ripple_blit(bgfx::Image& dst, const bgfx::Image& src, const Map& vecdist, const std::vector<std::uint32_t>& table);
// The same blended onto `dst` (the screen) with the transparency `level` (0..0x100); the samples wrap instead of reading 0.
int ripple_blend(bgfx::Image& dst, const bgfx::Image& src, bool src_has_alpha, const Map& vecdist, const std::vector<std::uint32_t>& table, int level);

} // namespace obgi::vecmap
