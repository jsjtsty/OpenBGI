#pragma once
#include <cstdint>
#include <map>
#include <vector>

namespace obgi {
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
    // The ripple mode (90:5B, sprite.c mode 4): the picture read through the vector + distance map `ripple_map` (-1 = not a ripple) and the
    // ring table of ripple definition `ripple_no` from phase `ripple_sel`; the level is the amplitude, the fade the only transparency.
    std::int32_t ripple_map = -1, rings = 0, ripple_no = 0, ripple_sel = 0;
    // An effector (91:60, effector.c): a full-screen object that takes everything drawn below it and writes back a distorted version:
    // mode 0 a displacement through vector maps, 1 a horizontal blur, 2 a ripple, 3 a rotation / zoom about a point.
    struct EffectorState {
        int mode = 1;                                        // Effector_Ctor starts as a gradient of type 0
        std::int32_t vec1 = -1, vec2 = -1, amount = 0;       // mode 0
        std::int32_t grad_type = 0;                          // mode 1
        std::int32_t map = -1, rings = 0, ripple_no = 0, ripple_sel = 0; // mode 2 (the level is the amplitude)
        std::int32_t cx = 0, cy = 0, angle = 0, sx = 0x10000, sy = 0x10000, smooth = 0;                    // mode 3 (16.16)
        std::int32_t d_cx = 0, d_cy = 0, d_angle = 0, d_sx = 0, d_sy = 0, curve = 0;                      // mode 3: changes reached at progress 1
    };
    bool effector = false;
    EffectorState fx;
    std::int32_t angle = 0, sx = 0x10000, sy = 0x10000;
    // 90:38 parameters the reference's DispObj_SetParam keeps (dispobj.c): 0x8001 no perspective scaling, 0x8000 snap to whole
    // pixels (with the snap mode), 0xC0 pass 90:31 on to the children, 0xC4 follow the 91:06 offset, 0xC5 a stored switch, 0x8101 the
    // sort mode (1: priority, then depth over its whole range, no class order), 0xFFFF flag bits, 0x7FFFFFFF the 16 script values
    bool no_perspective = false, snap = false, enabled_to_children = false, follow_offset = true;
    std::int32_t snap_mode = 0, param_c5 = 0;
    std::uint32_t sort_mode = 0, flag_bits = 0xFFFFFFFFu;
    std::int32_t user_values[16] = {};
    std::int32_t depth = 0, dist = 0, origin_x = 0, origin_y = 0;
    // The two 16.16 offsets added to the fixed position (91:37 and 91:36, dispobj.c: DispObj_EffFixedPos); a projected plane is placed and
    // scaled by the sum.
    std::int32_t fix_off1[3] = {}, fix_off2[3] = {};
    std::int32_t proj_x() const { return x + ((fix_off1[0] + fix_off2[0]) >> 16); }
    std::int32_t proj_y() const { return y + ((fix_off1[1] + fix_off2[1]) >> 16); }
    std::int32_t proj_depth() const { return depth + fix_off1[2] + fix_off2[2]; }
    // 91:3E: a slave object follows a master object by an offset. compose() keeps x,y = master + offset; a move
    // of the slave itself (90:33, 90:28/29: move_to) re-reads the offset from the new position, as the original
    // does. 90:56 sets x,y without telling the master.
    bool has_master = false;
    std::uint32_t master = 0;
    std::int32_t master_dx = 0, master_dy = 0;
    bool moved = false;
    void move_to(std::int32_t nx, std::int32_t ny) { x = nx, y = ny, moved = true; }
    // The 1-bit hit mask of 90:3C (dispobj.c: DispObj_BuildHitMask): one bit per pixel of its own size, set where the picture is opaque.
    // No mask means the whole picture hits.
    std::vector<std::uint8_t> hit_bits;
    std::uint32_t hit_w = 0, hit_h = 0;
    bool has_hit_mask = false;
    bool hidden = false; // 91:31: kept from being drawn without touching the shown, enabled or fade state
    bool is_shown() const { return visible && enabled && !hidden && fade < 0x100 && opacity > 0; }
};

// The 16.16 perspective factor of a depth (16.16) at a projection distance (pixels): 1.0 on the screen plane or without a
// distance, dist / (dist + depth) behind it, and growing linearly in front of it (dispobj.c: ProjectScale).
inline std::int32_t project_scale(std::int32_t depth, std::int32_t dist) {
    if (depth == 0 || dist == 0) return 0x10000;
    if (depth > 0) return static_cast<std::int32_t>((std::int64_t{dist} << 32) / ((std::int64_t{dist} << 16) + depth));
    return static_cast<std::int32_t>(((std::int64_t{dist} << 16) - depth) / dist);
}

// Pure, copyable logical scene. GPU allocations and transient render caches are elsewhere.
class SceneState {
  public:
    static constexpr std::uint32_t kBackgroundLower = 0x7C000000u, kBackgroundUpper = 0x7C000001u;
    Sprite& sprite(std::uint32_t id) {
        auto [it, inserted] = sprites_.try_emplace(id);
        if (inserted) created_[id] = ++counter_;
        return it->second;
    }
    Sprite* find_sprite(std::uint32_t id);
    void remove_sprite(std::uint32_t id) {
        for (auto& [child_id, child] : sprites_)
            if (child.has_master && child.master == id) child.has_master = false;
        sprites_.erase(id); created_.erase(id);
    }
    void set_visible(std::uint32_t id, bool value, int depth = 0);
    void set_level(std::uint32_t id, std::uint32_t value, int depth = 0);
    void set_progress(std::uint32_t id, std::uint32_t value, int depth = 0);
    void set_fade(std::uint32_t id, std::uint32_t value, int depth = 0);
    void set_opacity(std::uint32_t id, std::uint32_t value, int depth = 0);
    void resolve_masters();
    std::map<std::uint32_t, Sprite>& objects() { return sprites_; }
    const std::map<std::uint32_t, Sprite>& objects() const { return sprites_; }
    std::uint32_t creation_order(std::uint32_t id) const {
        auto it = created_.find(id);
        return it == created_.end() ? 0 : it->second;
    }
  private:
    std::map<std::uint32_t, Sprite> sprites_;
    std::map<std::uint32_t, std::uint32_t> created_;
    std::uint32_t counter_ = 0;
};
} // namespace obgi
