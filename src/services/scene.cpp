#include "services/scene.hpp"

namespace obgi {
Sprite* SceneState::find_sprite(std::uint32_t id) {
    // handle 0 is the background object, whose position, level, progress and visibility are the upper picture's
    if (id == 0)
        if (auto bg = sprites_.find(kBackgroundUpper); bg != sprites_.end()) return &bg->second;
    auto it = sprites_.find(id);
    return it == sprites_.end() ? nullptr : &it->second;
}

void SceneState::set_visible(std::uint32_t id, bool on, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->visible = on;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_visible(kv.first, on, depth + 1);
}

void SceneState::set_level(std::uint32_t id, std::uint32_t v, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->set_level(v);
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_level(kv.first, v, depth + 1);
}

void SceneState::set_progress(std::uint32_t id, std::uint32_t v, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->progress = v;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_progress(kv.first, v, depth + 1);
}

void SceneState::set_fade(std::uint32_t id, std::uint32_t v, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    if (sp->flag_bits & 1) sp->fade = v;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_fade(kv.first, v, depth + 1);
}

void SceneState::set_opacity(std::uint32_t id, std::uint32_t v, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->opacity = v;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_opacity(kv.first, v, depth + 1);
}

void SceneState::resolve_masters() {
    // masters first: a sprite is settled after the one it follows (chains are short; cycles are cut by `done`)
    std::map<std::uint32_t, bool> done;
    auto settle = [&](auto&& self, std::uint32_t id, int depth) -> void {
        Sprite* sp = find_sprite(id);
        if (!sp || done[id]) return;
        done[id] = true;
        if (!sp->has_master || depth > 16) return;
        self(self, sp->master, depth + 1);
        Sprite* ms = find_sprite(sp->master);
        if (!ms) return;
        if (sp->moved) { // moved by the script itself: remember the new offset
            sp->master_dx = sp->x - ms->x;
            sp->master_dy = sp->y - ms->y;
            sp->moved = false;
        }
        sp->x = ms->x + sp->master_dx;
        sp->y = ms->y + sp->master_dy;
    };
    for (auto& kv : sprites_) settle(settle, kv.first, 0);
}


} // namespace obgi
