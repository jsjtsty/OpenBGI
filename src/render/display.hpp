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
    // 91:3E: a slave object follows a master object by an offset. compose() keeps x,y = master + offset; a move
    // of the slave itself (90:33, 90:28/29: move_to) re-reads the offset from the new position, as the original
    // does. 90:56 sets x,y without telling the master.
    bool has_master = false;
    std::uint32_t master = 0;
    std::int32_t master_dx = 0, master_dy = 0;
    bool moved = false;
    void move_to(std::int32_t nx, std::int32_t ny) { x = nx, y = ny, moved = true; }
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
    Sprite& sprite(std::uint32_t id) { return sprites_[id]; } // creates on first use
    Sprite* find_sprite(std::uint32_t id);
    void remove_sprite(std::uint32_t id) { sprites_.erase(id); }
    std::map<std::uint32_t, Sprite>& sprites() { return sprites_; }
    // Shows or hides a sprite and, as the original does, every sprite attached to it by 91:3E.
    void set_visible(std::uint32_t id, bool on, int depth = 0);

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
};

} // namespace obgi
