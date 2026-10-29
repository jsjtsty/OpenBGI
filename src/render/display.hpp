#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

#include "core/error.hpp"
#include "render/renderer.hpp"

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
    // (16.16, behind the screen plane when positive) shrinks it by dist / (dist + depth). The picture's point (origin_x / 2,
    // origin_y / 2) lands on the projected position (the game passes the picture's size, which centres it: the reference
    // engine would put the bottom right corner there); with `scale_position` the position is scaled by the same factor.
    bool projected = false, scale_position = false;
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
    std::uint32_t draw_limit = 0;       // 90:09: objects below this priority are not drawn
    bool windows_shown = true;        // 90:0C: hides every window and what hangs on one
    std::uint32_t windows_level = 0;    // 90:0C: transparency added to all of them

    // Composes the sprites (by priority, then id) into the screen slot, which is returned for reading.
    Result<void> compose();
    // Moves every slave sprite (91:3E) to its master's position plus the stored offset.
    void resolve_masters();
    // compose() and draw the result as one frame of the renderer.
    Result<void> present();
    Result<std::vector<std::uint32_t>> read_screen();
    std::uint32_t screen_width() const;
    std::uint32_t screen_height() const;

  private:
    Display() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::map<std::uint32_t, Sprite> sprites_;
    std::map<std::uint32_t, std::uint32_t> created_;
    std::uint32_t created_counter_ = 0;
};

} // namespace obgi
