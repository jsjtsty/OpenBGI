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

} // namespace obgi::bgfx
