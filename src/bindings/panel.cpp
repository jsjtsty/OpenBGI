#include "bindings/panel.hpp"

#include <cstdio>
#include <cstdlib>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

constexpr std::uint32_t kSpriteBase = 0x7D000000; // display object ids of panel buttons: base | control << 12 | button

std::int32_t word(Ctx& c, Word addr) { return static_cast<std::int32_t>(c.read(addr, 4, false)); }

std::uint32_t button_sprite(std::uint32_t control, std::uint32_t index) { return kSpriteBase | (control & 0xFFF) << 12 | (index & 0xFFF); }

// the picture a button shows: its selected picture while it is its group's selection and has one
std::int32_t picture(const SystemState::PanelGroup& g, int index) {
    const auto& b = g.buttons[static_cast<std::size_t>(index)];
    return (index == g.selected && b.bmp_selected != -1) ? b.bmp_selected : b.bmp_normal;
}

void show(SystemState& s, const SystemState::IconControl& ic, const SystemState::PanelButton& b, std::int32_t bmp) {
    if (!s.display || b.sprite == 0) return;
    Sprite& sp = s.display->sprite(b.sprite);
    const Sprite* win = s.display->find_sprite(ic.window);
    sp.slot = static_cast<std::uint32_t>(bmp);
    sp.x = (win ? win->x : 0) + b.x;
    sp.y = (win ? win->y : 0) + b.y;
    sp.priority = (win ? win->priority : 0) + 1; // over the window itself
    sp.blend = 1, sp.alpha = 0;
    sp.visible = bmp >= 0 && s.display->slot(static_cast<std::uint32_t>(bmp)) != nullptr;
}

} // namespace

std::uint32_t panel_start(SystemState& s, std::uint32_t id, Ctx& c, std::uint32_t desc) {
    auto it = s.icon_controls.find(id);
    if (it == s.icon_controls.end()) return 1;
    auto& ic = it->second;
    panel_stop(s, id);
    // the descriptor (0x28 bytes): group count, group table, current group, modal, mouse, input mask, key map, select on
    // decide, input disabled; a group (0x40 bytes): button count, columns, button table, selected, ...; a button (0xC4 bytes):
    // enabled, shown, x, y, ox, oy, frames, frame ms, normal / focus / selected / selected+focus bitmaps, hit mask, ...
    const std::int32_t group_count = word(c, desc);
    const Word groups = static_cast<Word>(word(c, desc + 4));
    if (!c.ok() || group_count <= 0 || group_count > 0x100) return 2;
    std::vector<SystemState::PanelGroup> out;
    for (std::int32_t g = 0; g < group_count; ++g) {
        const Word ga = groups + 0x40 * static_cast<Word>(g);
        const std::int32_t count = word(c, ga), columns = word(c, ga + 4);
        const Word buttons = static_cast<Word>(word(c, ga + 8));
        if (!c.ok() || count <= 0 || count > 0x100) return 2;
        SystemState::PanelGroup pg;
        pg.columns = columns < 1 ? 1 : (columns > count ? count : columns);
        const std::int32_t sel = word(c, ga + 0xC);
        pg.selected = sel >= 0 && sel < count ? sel : -1;
        for (std::int32_t i = 0; i < count; ++i) {
            const Word ba = buttons + 0xC4 * static_cast<Word>(i);
            SystemState::PanelButton b;
            b.enabled = word(c, ba) != 0;
            b.shown = word(c, ba + 4) != 0;
            b.x = word(c, ba + 8), b.y = word(c, ba + 0xC);
            b.bmp_normal = word(c, ba + 0x20), b.bmp_selected = word(c, ba + 0x28);
            b.flags = static_cast<std::uint32_t>(word(c, ba + 0xC0));
            if (!c.ok()) return 3;
            pg.buttons.push_back(b);
        }
        out.push_back(std::move(pg));
    }
    const std::int32_t cur = word(c, desc + 8);
    ic.groups = std::move(out);
    ic.current_group = cur >= 0 && cur < group_count ? cur : -1;
    // from 1.588 on a button is drawn when `shown`, unless its flags say 0x40 (never)
    std::uint32_t k = 0;
    for (auto& g : ic.groups)
        for (std::size_t i = 0; i < g.buttons.size(); ++i, ++k) {
            auto& b = g.buttons[i];
            if (!b.shown || (b.flags & 0x40) || b.bmp_normal == -1) continue;
            b.sprite = button_sprite(id, k);
            show(s, ic, b, picture(g, static_cast<int>(i)));
            if (std::getenv("OBGI_DBGPANEL")) std::fprintf(stderr, "panel %u button %zu en=%d bmp=%d at %d,%d win=%X visible=%d prio=%u winsprite=%d\n", id, i, b.enabled, picture(g, static_cast<int>(i)), b.x, b.y, ic.window, s.display && s.display->find_sprite(b.sprite) ? s.display->find_sprite(b.sprite)->visible : -1, s.display ? s.display->find_sprite(b.sprite)->priority : 0u, s.display && s.display->find_sprite(ic.window) ? 1 : 0);
        }
    return 0;
}

void panel_select(SystemState& s, std::uint32_t id, int group, int index) {
    auto it = s.icon_controls.find(id);
    if (it == s.icon_controls.end() || group < 0 || group >= static_cast<int>(it->second.groups.size())) return;
    auto& ic = it->second;
    auto& g = ic.groups[static_cast<std::size_t>(group)];
    if (index >= static_cast<int>(g.buttons.size())) return;
    const int old = g.selected;
    g.selected = index;
    for (int i : {old, index})
        if (i >= 0) show(s, ic, g.buttons[static_cast<std::size_t>(i)], picture(g, i));
}

void panel_sync(SystemState& s) {
    if (!s.display) return;
    for (auto& [id, ic] : s.icon_controls) {
        const Sprite* win = s.display->find_sprite(ic.window);
        for (auto& g : ic.groups)
            for (std::size_t i = 0; i < g.buttons.size(); ++i) {
                auto& b = g.buttons[i];
                if (b.sprite == 0) continue;
                Sprite* sp = s.display->find_sprite(b.sprite);
                if (!sp) continue;
                sp->x = (win ? win->x : 0) + b.x;
                sp->y = (win ? win->y : 0) + b.y;
                sp->priority = (win ? win->priority : 0) + 1;
                sp->alpha = win ? win->alpha : 0;
                sp->visible = (!win || win->visible) && s.display->slot(sp->slot) != nullptr;
            }
    }
}

void panel_stop(SystemState& s, std::uint32_t id) {
    auto it = s.icon_controls.find(id);
    if (it == s.icon_controls.end()) return;
    for (auto& g : it->second.groups)
        for (auto& b : g.buttons)
            if (b.sprite && s.display) s.display->remove_sprite(b.sprite);
    it->second.groups.clear();
}

} // namespace obgi
