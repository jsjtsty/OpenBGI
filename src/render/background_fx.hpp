#pragma once
#include <array>
#include <cstdint>
#include <vector>

// The picture kernels behind the background types of the reference engine (gfx/background/*.c and the blit_*.c routines they
// call), on packed 0xAARRGGBB pixels. Each follows the reference's integer arithmetic; they are what Display runs on the CPU
// when the script selects a background type that is more than a picture or two (90:41, 42, 44, 46, 48, 49, 4A).
namespace obgi::bgfx {

struct Image {
    std::uint32_t w = 0, h = 0;
    std::vector<std::uint32_t> px;
};

// Type 2 (crossfade.c): `second` with `first` mixed over it by `level` (0 = first, 0x100 = second); Bmp_BlitEffect 0xF0.
Image crossfade(const Image& first, const Image& second, int level);
// Type 2 with a solid colour as the second picture: toward black (effect 0xC0) or white (0xC1); the alpha stays.
Image toward_colour(const Image& first, bool white, int level);
// Type 3 (tiled.c): the w x h window at (ox, oy) of the 2w x 2h plane of four pictures (top left, top right, bottom left, bottom right).
Image tiled(const std::array<const Image*, 4>& quads, int ox, int oy);
// Type 7 (blit_displace.c: Blit_Gradient): a horizontal box blur of 2 * level + 1 pixels; `edge` 0 pads with black, 1 repeats the edge.
Image box_blur(const Image& src, int edge, int level);
// Type 9 (stretch.c via blit_rotate.c: Blit_Scale2, bilinear): the part of `src` given by the 16.16 origin and full size, scaled onto w x h.
Image scale2(const Image& src, std::uint32_t w, std::uint32_t h, std::int32_t ox, std::int32_t oy, std::int32_t full_w, std::int32_t full_h,
             std::int32_t sx, std::int32_t sy);
// Type 10 (zoom.c via Blit_RotateCentred, nearest): `src` zoomed (16.16) and rotated (16.16 degrees) about the centre of a w x h picture.
Image rotate_centred(const Image& src, std::uint32_t w, std::uint32_t h, std::int32_t zoom, std::int32_t angle);
// Type 11 (mosaic.c via Blit_Flip): blocks of `amount` + 1 pixels of `src` over `dst`; `extra` is the blocks' transparency (0 replaces).
void mosaic(Image& dst, const Image& src, int amount, int extra);

// blit.c: Blit_CrossfadeTo, the two pictures of a sprite (90:58, 90:5C's second bitmap) mixed by `ratio` (0 = a, 0x100 = b): a plain lerp of the
// four bytes without alpha, weighted by the two alphas with it.
Image crossfade_to(const Image& a, const Image& b, int ratio, bool has_alpha);

// The colour filters of the Filter object (filter.c, blit_filter.c, blit.c: Blit_Tint) applied in place to the screen's pixels at the
// level (0..0x100; a strength). Kind 0 blends toward the colour (a black colour dims), 1 adds colour * level (black dims), 2 tints by
// luminance, 3 blends toward the picture XOR the colour; the top byte of the result is 0 except for kind 3, which keeps it.
void filter_kind(std::vector<std::uint32_t>& px, int kind, std::uint32_t colour, int level);
// Kind 0 modulated per pixel by a gray bitmap of the screen's size (Blit_TintByGray); `param` is the transition curve.
void filter_by_gray(std::vector<std::uint32_t>& px, std::uint32_t colour, const std::vector<std::uint32_t>& gray, int param, int level);

// The original's Ease(): progress t (0..0x1000000 = 1.0) to a 16.16 factor. Curve 0 is linear, 1..3 sine easing, 4..15 powers.
std::int32_t ease(std::int32_t t, int curve);

// A picture scaled by (sx, sy) and rotated by `angle` (16.16 degrees) about the point (ox16, oy16) (16.16 pixels of the picture),
// as the transform sprites draw it (sprite.c: Sprite_BoundsProjected for the box). `anchor` is where the origin lands in the result.
struct Transformed {
    Image image;
    std::int32_t anchor_x = 0, anchor_y = 0;
};
Transformed transform(const Image& src, std::int32_t ox16, std::int32_t oy16, std::int32_t angle, std::int32_t sx, std::int32_t sy, bool smooth);

// blit_xform.c: Blit_XformCopy. `dst` is replaced entirely by `src` scaled by (sx, sy) and rotated by `angle` (16.16) about its point
// (cx16, cy16), which lands on the dst point (x16, y16); outside the source the result is black / transparent; the colours are then scaled
// by (0x100 - level) / 256 with the alpha kept, and a level of 0x100 or more clears dst.
void xform_copy(Image& dst, std::int32_t x16, std::int32_t y16, const Image& src, std::int32_t cx16, std::int32_t cy16, std::int32_t angle,
                std::int32_t sx, std::int32_t sy, int level, bool smooth);

// blit_filter.c, the colour modes of "91 1D": src through mode 0 copy, 1 (Blit_FilterKind3), 2 (Blit_FilterKind2), 3 (Blit_TintKeepAlpha) or
// 4 (Blit_AddColour); the mode must be 0..4.
Image colour_mode(const Image& src, int mode, std::uint32_t colour, int level);

// blit_scale.c: Blit_GatherAdd of the RGB32 `src` into the RGB32 `dst` ("91 1B"): the pixels drawn toward the centre by the 16.16 rates
// and added with saturation, attenuated by level (0..0x100).
void gather_add(Image& dst, const Image& src, std::uint32_t cx, std::uint32_t cy, std::uint32_t level);

// blit_xform.c: Blit_ApplyMask over the overlap, in place on an ARGB32 picture ("91 1E"): the alpha becomes alpha * mask >> 8, the mask
// being the gray bitmap's pixel (px + x, py + y); `gray` holds one pixel per word.
void apply_gray(Image& dst, const std::vector<std::uint32_t>& gray, std::uint32_t gw, std::uint32_t gh, int x, int y);

} // namespace obgi::bgfx
