#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "render/renderer.hpp"
#include "services/scene.hpp"
#include "render/vector_maps.hpp"

namespace obgi {

// Pixel formats of the original bitmap type (analysis/混合公式.md). Every slot stores one uint32 per
// pixel on the GPU whatever its format, packed as blend.glsl describes.
enum class SlotFormat : std::uint8_t { Rgb555 = 0, Bgrx = 1, Bgra = 2, Gray8 = 3 };

struct SlotInfo {
    std::uint32_t width = 0, height = 0;
    SlotFormat format = SlotFormat::Bgrx;
};

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
    std::uint64_t slot_generation(std::uint32_t id) const;
    std::uint64_t reserve_slot_load(std::uint32_t id);
    std::uint64_t slot_load_ticket(std::uint32_t id) const;
    // Fills every pixel with a raw packed value.
    Result<void> fill_slot(std::uint32_t id, std::uint32_t packed);
    // Creates or replaces a slot with the given packed pixels (w * h values).
    Result<void> upload_slot(std::uint32_t id, std::uint32_t w, std::uint32_t h, SlotFormat format,
                             std::span<const std::uint32_t> packed);
    // Copies/blends a rectangle of src into dst, clipped to both. mode/alpha are the script values.
    Result<void> blit(std::uint32_t dst, std::int32_t dx, std::int32_t dy, std::uint32_t src, std::int32_t sx,
                      std::int32_t sy, std::uint32_t w, std::uint32_t h, std::uint32_t mode, std::uint32_t alpha);
    // Alpha-weighted image crossfade into a clipped destination rectangle (90:19 without a gray map).
    Result<void> crossfade_slot(std::uint32_t dst, std::int32_t dx, std::int32_t dy, std::uint32_t src, std::uint32_t ratio);
    Result<std::vector<std::uint32_t>> read_slot(std::uint32_t id);

    // ---- sprites ----
    SceneState& scene() { return scene_; }
    const SceneState& scene() const { return scene_; }
    Sprite& sprite(std::uint32_t id) { return scene_.sprite(id); }
    // The background (object 0) is two sprites: the lower picture and the upper one over it (90:43); find_sprite(0) is the upper.
    static constexpr std::uint32_t kBackgroundLower = 0x7C000000u, kBackgroundUpper = 0x7C000001u;
    Sprite* find_sprite(std::uint32_t id);
    void remove_sprite(std::uint32_t id) { scene_.remove_sprite(id); }
    std::map<std::uint32_t, Sprite>& sprites() { return scene_.objects(); }
    // Shows or hides a sprite and, as the original does, every sprite attached to it by 91:3E.
    void set_visible(std::uint32_t id, bool on, int depth = 0);
    void set_level(std::uint32_t id, std::uint32_t v, int depth = 0);
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
        std::int32_t amount = 0;                                 // type 6: non-zero samples bilinearly (bmp[1], bmp[2] are the vector maps, -1 = none)
        std::int32_t rings = 0, ripple_no = 0, selector = 0;     // type 8: bmp[1] is the vector + distance map; the level is the amplitude
    } bg_fx;
    // B0:08, the screen quake: present() shows the whole screen shifted by this many pixels (window.c: Window_PresentOffset)
    std::int32_t present_dx = 0, present_dy = 0;
    std::uint64_t vecmap_epoch = 0; // bumped whenever a vector map or a ripple definition changes, so the pictures made from them redraw
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
    Result<void> compose(std::optional<std::uint32_t> max_priority = std::nullopt);
    // Render to a new screen-sized bitmap without replacing the displayed back buffer.
    // A priority limit includes every sort key through that priority (90:05).
    Result<void> snapshot(std::uint32_t slot, std::optional<std::uint32_t> max_priority = std::nullopt);
    // The screen rectangle (inclusive) a sprite's picture covers, as the compositor draws it (rotations are not followed); false for an
    // object without a picture (dispobj.c: DispObj_ScreenRect, used by the click targets, 90:FC).
    bool screen_rect(std::uint32_t id, std::int32_t& left, std::int32_t& top, std::int32_t& right, std::int32_t& bottom);
    // Moves every slave sprite (91:3E) to its master's position plus the stored offset.
    void resolve_masters();
    // compose() and draw the result as one frame of the renderer.
    Result<void> present();
    Result<std::vector<std::uint32_t>> read_screen();
    Result<void> resize_screen(std::uint32_t width, std::uint32_t height);
    std::uint32_t screen_width() const;
    std::uint32_t screen_height() const;

  private:
    Result<void> render_background_fx();
    Result<void> render_maps();
    Display() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    SceneState scene_;
};

} // namespace obgi
