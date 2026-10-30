#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "render/renderer.hpp"
#include "render/vector_maps.hpp"

namespace obgi {

// Pixel formats of the original bitmap type (analysis/混合公式.md). Every slot stores one uint32 per
// pixel on the GPU whatever its format, packed as blend.glsl describes.
enum class SlotFormat : std::uint8_t { Rgb555 = 0, Bgrx = 1, Bgra = 2, Gray8 = 3 };

struct SlotInfo {
    std::uint32_t width = 0, height = 0;
    SlotFormat format = SlotFormat::Bgrx;
};

// A display object that shows (part of) an image slot at a position (script op 90:56 and friends).
struct Sprite {
    std::uint32_t slot = 0;
    std::int32_t x = 0, y = 0;
    std::uint32_t blend = 1;     // script-level blend mode
    // The script's "alpha" A (0..0x100) is handed to the blend kernel unchanged (analysis/混合公式.md):
    // for the alpha-over modes (1, 0x20..0x24) it is a *transparency* (0 = opaque, 0x100 = invisible),
    // for the others a strength. The object's two extra factors are neutral by default and not modelled.
    std::uint32_t alpha = 0;
    std::uint32_t priority = 0;
    bool visible = true;
    // 90:31 enabled, 90:34 fade (an extra transparency, passed on to the children), 90:39 opacity (0x100 = opaque), 90:36 /
    // 90:37 draw offsets. They combine with `alpha` according to the blend family (see Display::compose).
    bool enabled = true;
    std::int32_t sort_bias = 0;              // 90:38 property 0x8100: added to the sort key
    bool background = false;                // the background layers are not held back by the draw limit (90:09)
    int filter_kind = -1;                    // 90:65 / 90:66: a filter object (no picture) changes what is drawn below it; -1 = not one
    std::uint32_t filter_colour = 0;         // 0x00RRGGBB; `alpha` is the filter's level
    std::uint32_t filter_gray = 0xFFFFFFFFu; // 90:66: the screen-sized gray bitmap that modulates the tint per pixel (0xFFFFFFFF = uniform)
    std::int32_t filter_param = 0;           // ... and its transition curve
    // 90:35 progress (0..0x100) reveals the picture through the gray slot `wipe_gray` (90:5A) while it is below 0x100
    std::uint32_t progress = 0x100, wipe_gray = 0xFFFFFFFFu, wipe_mode = 0;
    // The background's two-picture type (90:43) reveals the upper picture through a gray map aligned with the screen instead:
    // `progress` then holds the script's level (0 = all shown, 0x100 = all hidden) and `wipe_mode` its curve parameter 0..15
    // (blit_filter.c: Blit_ThroughGray).
    bool through_gray = false;
    std::uint32_t fade = 0, opacity = 0x100;
    std::int32_t off_x = 0, off_y = 0, off2_x = 0, off2_y = 0;
    // the background's upper picture is drawn at minus the object's position (a view origin, not a place)
    bool flip_position = false;
    std::int32_t script_x() const { return flip_position ? -x : x; }
    std::int32_t script_y() const { return flip_position ? -y : y; }
    void script_move_to(std::int32_t nx, std::int32_t ny) { move_to(flip_position ? -nx : nx, flip_position ? -ny : ny); }
    // A projected plane (90:5C, sprite.c mode 5): x, y are the script's position, relative to the screen centre, and `depth`
    // (16.16, behind the screen plane when positive) shrinks it by dist / (dist + depth). The picture's point (origin_x,
    // origin_y), in its own pixels, lands on the projected position (sprite.c: Sprite_BoundsProjected); scripts pass the picture's
    // base size (92:16, the one in its file header) as the origin. With `scale_position` the position is scaled by the same factor.
    bool projected = false, scale_position = false;
    // A second picture of the same size cross-faded in by `mix_ratio` (0 = the first, 0x100 = the second; 90:58, 90:5C), and what a
    // level change drives (sprite.c: Sprite_SetLevel): -1 / 0 the level, 1 the mix ratio, 2 both, anything else nothing.
    std::uint32_t slot2 = 0xFFFFFFFFu, mix_ratio = 0;
    std::int32_t level_route = -1;
    void set_level(std::uint32_t v) {
        switch (level_route) {
        case 2: alpha = v, mix_ratio = v; break;
        case 1: mix_ratio = v; break;
        case 0: case -1: alpha = v; break;
        default: break;
        }
    }
    std::uint32_t level() const { return level_route == 1 ? mix_ratio : alpha; }
    // The transform mode (90:59): scaled by (sx, sy) and rotated by `angle` about the origin (origin_x, origin_y) of the picture, which
    // lands on the position; projected planes use the same angle with the scale of their depth.
    bool xform = false, smooth = false;
    std::int32_t angle = 0, sx = 0x10000, sy = 0x10000;
    // 90:38 parameters the reference's DispObj_SetParam keeps (dispobj.c): 0x8001 no perspective scaling, 0x8000 snap to whole
    // pixels (with the snap mode), 0xC0 pass 90:31 on to the children, 0xC4 follow the 91:06 offset, 0xC5 a stored switch, 0x8101 the
    // sort mode (1: priority, then depth over its whole range, no class order), 0xFFFF flag bits, 0x7FFFFFFF the 16 script values
    bool no_perspective = false, snap = false, enabled_to_children = false, follow_offset = true;
    std::int32_t snap_mode = 0, param_c5 = 0;
    std::uint32_t sort_mode = 0, flag_bits = 0;
    std::int32_t user_values[16] = {};
    std::int32_t depth = 0, dist = 0, origin_x = 0, origin_y = 0;
    // 91:3E: a slave object follows a master object by an offset. compose() keeps x,y = master + offset; a move
    // of the slave itself (90:33, 90:28/29: move_to) re-reads the offset from the new position, as the original
    // does. 90:56 sets x,y without telling the master.
    bool has_master = false;
    std::uint32_t master = 0;
    std::int32_t master_dx = 0, master_dy = 0;
    bool moved = false;
    void move_to(std::int32_t nx, std::int32_t ny) { x = nx, y = ny, moved = true; }
    bool is_shown() const { return visible && enabled && fade < 0x100 && opacity > 0; }
};

// The 16.16 perspective factor of a depth (16.16) at a projection distance (pixels): 1.0 on the screen plane or without a
// distance, dist / (dist + depth) behind it, and growing linearly in front of it (dispobj.c: ProjectScale).
inline std::int32_t project_scale(std::int32_t depth, std::int32_t dist) {
    if (depth == 0 || dist == 0) return 0x10000;
    if (depth > 0) return static_cast<std::int32_t>((std::int64_t{dist} << 32) / ((std::int64_t{dist} << 16) + depth));
    return static_cast<std::int32_t>(((std::int64_t{dist} << 16) - depth) / dist);
}

bool blend_mode_supported(std::uint32_t mode);

// Image slots and sprites on the GPU. Slot-to-slot blits and the per-frame composition run as compute
// kernels using the original engine's integer blend formulas; the finished frame is shown through
// the Renderer. The Renderer must outlive the Display.
class Display {
  public:
    static Result<std::unique_ptr<Display>> create(Renderer& renderer, std::uint32_t width = 800, std::uint32_t height = 600);
    ~Display();

    // ---- image slots (script ids 0..0x3fff) ----
    Result<void> create_slot(std::uint32_t id, std::uint32_t w, std::uint32_t h, SlotFormat format);
    Result<void> free_slot(std::uint32_t id);
    const SlotInfo* slot(std::uint32_t id) const;
    // Fills every pixel with a raw packed value.
    Result<void> fill_slot(std::uint32_t id, std::uint32_t packed);
    // Creates or replaces a slot with the given packed pixels (w * h values).
    Result<void> upload_slot(std::uint32_t id, std::uint32_t w, std::uint32_t h, SlotFormat format,
                             std::span<const std::uint32_t> packed);
    // Copies/blends a rectangle of src into dst, clipped to both. mode/alpha are the script values.
    Result<void> blit(std::uint32_t dst, std::int32_t dx, std::int32_t dy, std::uint32_t src, std::int32_t sx,
                      std::int32_t sy, std::uint32_t w, std::uint32_t h, std::uint32_t mode, std::uint32_t alpha);
    Result<std::vector<std::uint32_t>> read_slot(std::uint32_t id);

    // ---- sprites ----
    Sprite& sprite(std::uint32_t id) { // creates on first use; the creation order breaks ties in the draw order (dispobj.c: DispObj_SortKey)
        auto [it, inserted] = sprites_.try_emplace(id);
        if (inserted) created_[id] = ++created_counter_;
        return it->second;
    }
    // The background (object 0) is two sprites: the lower picture and the upper one over it (90:43); find_sprite(0) is the upper.
    static constexpr std::uint32_t kBackgroundLower = 0x7C000000u, kBackgroundUpper = 0x7C000001u;
    Sprite* find_sprite(std::uint32_t id);
    void remove_sprite(std::uint32_t id) { sprites_.erase(id), created_.erase(id); }
    std::map<std::uint32_t, Sprite>& sprites() { return sprites_; }
    // Shows or hides a sprite and, as the original does, every sprite attached to it by 91:3E.
    void set_visible(std::uint32_t id, bool on, int depth = 0);
    void set_progress(std::uint32_t id, std::uint32_t v, int depth = 0);
    void set_fade(std::uint32_t id, std::uint32_t v, int depth = 0);
    void set_opacity(std::uint32_t id, std::uint32_t v, int depth = 0);
    // The background types that are more than one or two pictures (gfx/background/*.c): 2 crossfade (90:41), 3 tiled view (90:42),
    // 5 flip book (90:44), 7 blur (90:46), 9 stretched view (90:48), 10 zoom (90:49), 11 mosaic (90:4A). The object's level and
    // position are those of the upper background sprite (handle 0), so the tweens that drive them need no special case; compose()
    // renders the picture on the CPU into a temporary slot shown by the lower background sprite. type 0 = none of these.
    struct BackgroundFx {
        int type = 0;
        std::uint64_t revision = 0; // bumped by every change of the fields below
        std::int32_t bmp[4] = {-1, -1, -1, -1}, second = -1;   // bitmaps; second: a bitmap, 0x7000 black or 0x7001 white
        std::vector<std::int32_t> frames;                        // type 5
        std::int32_t origin_x = 0, origin_y = 0;                 // type 3: the window's top left corner
        std::int32_t edge = 0;                                   // type 7: 0 pads with black, 1 repeats the edge
        std::int32_t view_w = 0, view_h = 0, target_x = 0, target_y = 0, target_w = 0, target_h = 0; // type 9
        std::int32_t scale = 0x10000, angle = 0, scale_delta = 0, angle_delta = 0;                    // type 10
        std::int32_t style = 0, link = 0;                        // type 11
    } bg_fx;
    // A scrolling tile map (90:70-7A, map.c): a window of cols x rows tiles of chip_w x chip_h pixels onto a terrain of 16-bit tile
    // numbers (0xFFFF = empty), the tiles being cut row by row from the chipset picture. compose() renders the window on the CPU
    // into a temporary slot that the map's sprite (handle 0xA0000000 | index) shows.
    struct MapData {
        bool sized = false, has_terrain = false, view_valid = false, wrap = false;
        std::int32_t cols = 0, rows = 0, chip_w = 0, chip_h = 0, chipset = -1;
        std::int32_t terr_w = 0, terr_h = 0, col = -1, row = -1, off_x = -1, off_y = -1;
        std::vector<std::uint16_t> terrain;
        std::uint64_t revision = 0;
        std::vector<std::int64_t> key; // what the picture in the temporary slot was last made for
    };
    std::map<std::uint32_t, MapData> maps;
    // The bitmaps of the vector-map modes (vector, height, vector + distance; vector_maps.hpp) live here, under the slot numbers the
    // scripts give them; a number is either a GPU slot or one of these. The eight ripple definitions (92:00, 92:01) too.
    std::map<std::uint32_t, vecmap::Map> vecmaps;
    vecmap::RippleDefs ripples;
    bool bg_visible = true;  // 90:4C
    std::int32_t bg_mode = 1; // 90:4C: 0 paints black, anything else the type's picture
    std::uint32_t draw_limit = 0;       // 90:09: objects below this priority are not drawn
    bool windows_shown = true;        // 90:0C: hides every window and what hangs on one
    std::uint32_t windows_level = 0;    // 90:0C: transparency added to all of them

    // Composes the sprites (by priority, then id) into the screen slot, which is returned for reading.
    Result<void> compose();
    // The screen rectangle (inclusive) a sprite's picture covers, as the compositor draws it (rotations are not followed); false for an
    // object without a picture (dispobj.c: DispObj_ScreenRect, used by the click targets, 90:FC).
    bool screen_rect(std::uint32_t id, std::int32_t& left, std::int32_t& top, std::int32_t& right, std::int32_t& bottom);
    // Moves every slave sprite (91:3E) to its master's position plus the stored offset.
    void resolve_masters();
    // compose() and draw the result as one frame of the renderer.
    Result<void> present();
    Result<std::vector<std::uint32_t>> read_screen();
    std::uint32_t screen_width() const;
    std::uint32_t screen_height() const;

  private:
    Result<void> render_background_fx();
    Result<void> render_maps();
    Display() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::map<std::uint32_t, Sprite> sprites_;
    std::map<std::uint32_t, std::uint32_t> created_;
    std::uint32_t created_counter_ = 0;
};

} // namespace obgi
