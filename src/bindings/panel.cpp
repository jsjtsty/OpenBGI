#include "bindings/panel.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <vector>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

constexpr std::uint32_t kSpriteBase = 0x7D000000; // display object ids of panel buttons: base | control << 12 | button

std::int32_t word(Ctx& c, Word addr) { return static_cast<std::int32_t>(c.read(addr, 4, false)); }

std::uint32_t button_sprite(std::uint32_t control, std::uint32_t index) { return kSpriteBase | (control & 0xFFF) << 12 | (index & 0xFFF); }

bool sprite_hit(Display& display, std::uint32_t id, int x, int y) {
    Sprite* sp = display.find_sprite(id);
    if (!sp || !sp->is_shown()) return false;
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
    if (!display.screen_rect(id, left, top, right, bottom) || x < left || x > right || y < top || y > bottom) return false;
    if (!sp->has_hit_mask) return true;
    const auto px = static_cast<std::uint32_t>(x - left), py = static_cast<std::uint32_t>(y - top);
    if (px >= sp->hit_w || py >= sp->hit_h) return false;
    const std::size_t pitch = (std::size_t{sp->hit_w} + 7) >> 3;
    return ((sp->hit_bits[std::size_t{py} * pitch + (px >> 3)] >> (px & 7)) & 1) != 0;
}

// the picture a button shows: normal / focus / selected / selected+focus, falling back to the nearest one that exists
std::int32_t picture(const SystemState::PanelGroup& g, int index, bool focus = false) {
    const auto& b = g.buttons[static_cast<std::size_t>(index)];
    const bool sel = index == g.selected && b.bmp_selected != -1;
    if (sel && focus && b.bmp_selected_focus != -1) return b.bmp_selected_focus;
    if (sel) return b.bmp_selected;
    if (focus && b.bmp_focus != -1) return b.bmp_focus;
    return b.bmp_normal;
}

bool hovered(const SystemState::IconControl& ic, int group, int index) { return ic.hover_group == group && ic.hover_index == index; }

std::pair<std::int32_t, std::int32_t> panel_offset(const SystemState& s, const SystemState::IconControl& ic) {
    const auto index = ic.window & 0xFFFFFFu;
    if (ic.basic && (ic.window & 0xFF000000u) == 0xB0000000u && index < s.planes.size() && s.planes[index])
        return {s.planes[index]->area[0], s.planes[index]->area[1]};
    return {0, 0};
}

void show(SystemState& s, const SystemState::IconControl& ic, const SystemState::PanelButton& b, std::int32_t bmp) {
    if (!s.display || b.sprite == 0) return;
    Sprite& sp = s.display->sprite(b.sprite);
    const Sprite* win = s.display->find_sprite(ic.window);
    sp.slot = static_cast<std::uint32_t>(bmp);
    const auto [ox, oy] = panel_offset(s, ic);
    sp.x = (win ? win->x : 0) + ox + b.x;
    sp.y = (win ? win->y : 0) + oy + b.y;
    sp.priority = (win ? win->priority : 0) + 1; // over the window itself
    sp.blend = 1, sp.alpha = 0;
    sp.visible = bmp >= 0 && s.display->slot(static_cast<std::uint32_t>(bmp)) != nullptr;
    sp.hit_bits.clear(), sp.hit_w = sp.hit_h = 0, sp.has_hit_mask = false;
    if (b.hit_mask == -2) {
        const SlotInfo* slot = s.display->slot(sp.slot);
        sp.hit_w = slot ? slot->width : 0, sp.hit_h = slot ? slot->height : 0;
        sp.has_hit_mask = true;
        sp.hit_bits.assign((std::size_t{sp.hit_w + 7} >> 3) * sp.hit_h, 0);
    } else if (b.hit_mask >= 0 && b.hit_mask < 0x4000) {
        if (const SlotInfo* mask = s.display->slot(static_cast<std::uint32_t>(b.hit_mask))) {
            auto pixels = s.display->read_slot(static_cast<std::uint32_t>(b.hit_mask));
            if (pixels) {
                sp.hit_w = mask->width, sp.hit_h = mask->height, sp.has_hit_mask = true;
                const std::size_t pitch = (std::size_t{mask->width} + 7) >> 3;
                sp.hit_bits.assign(pitch * mask->height, 0);
                for (std::uint32_t y = 0; y < mask->height; ++y)
                    for (std::uint32_t x = 0; x < mask->width; ++x) {
                        const std::uint32_t px = (*pixels)[std::size_t{y} * mask->width + x];
                        const bool opaque = mask->format == SlotFormat::Bgra ? (px >> 24) != 0 :
                            mask->format == SlotFormat::Bgrx ? (px & 0xFFFFFF) != 0 : (px & 0xFF) != 0;
                        if (opaque) sp.hit_bits[std::size_t{y} * pitch + (x >> 3)] |= static_cast<std::uint8_t>(1u << (x & 7));
                    }
            }
        }
    }
}

// The plain panel (90:B8 / 90:BA, the confirmation windows and the like): the same buttons and events, a smaller descriptor.
// descriptor: group count, group table, current group, ...; group (0x34 bytes): button count (low 16 bits; the high 16 are the
// columns), button table, selected button, ...; button (0x3C bytes): enabled, x, y, normal bitmap, selected bitmap, ?, focus
// bitmap (-2: none), ... A button is drawn when it is enabled. Returns 1 for an unknown control, 4 when it has started, 2 / 3 as 91:BA.
std::uint32_t panel_start_basic(SystemState& s, SystemState::IconControl& ic, std::uint32_t id, Ctx& c, Word desc) {
    if (!ic.groups.empty()) return 4;
    ic.modal = false, ic.mouse = false;
    const std::int32_t group_count = word(c, desc);
    const Word groups = static_cast<Word>(word(c, desc + 4));
    if (!c.ok() || group_count <= 0 || group_count > 0x100) return 2;
    std::vector<SystemState::PanelGroup> out;
    for (std::int32_t g = 0; g < group_count; ++g) {
        const Word ga = groups + 0x34 * static_cast<Word>(g);
        const std::uint32_t head = static_cast<std::uint32_t>(word(c, ga));
        const std::int32_t count = static_cast<std::int32_t>(head & 0xFFFF), wide = static_cast<std::int32_t>(head >> 16);
        const Word buttons = static_cast<Word>(word(c, ga + 4));
        if (!c.ok() || count <= 0 || count > 0x100) return 2;
        SystemState::PanelGroup pg;
        pg.columns = count < wide ? count : (wide != 0 ? wide : count);
        const std::int32_t sel = word(c, ga + 8);
        pg.selected = sel >= 0 && sel < count ? sel : -1;
        pg.selectable = word(c, ga + 0xC) != 0, pg.hover_selects = word(c, ga + 0x10) != 0;
        pg.click_decides = word(c, ga + 0x14) != 0, pg.link_id = word(c, ga + 0x18);
        for (std::int32_t i = 0; i < count; ++i) {
            const Word ba = buttons + 0x3C * static_cast<Word>(i);
            SystemState::PanelButton b;
            b.enabled = b.shown = word(c, ba) != 0;
            b.x = word(c, ba + 4), b.y = word(c, ba + 8);
            // enabled, x, y, normal, selected (-1: the normal one), focus (-1: none), hit mask (-2: cannot be hit), ..., hotkey at 0x38
            b.bmp_normal = word(c, ba + 0xC), b.bmp_selected = word(c, ba + 0x10);
            const std::int32_t focus = word(c, ba + 0x14);
            b.bmp_focus = focus < 0 ? -1 : focus;
            b.bmp_selected_focus = b.bmp_focus;
            b.hit_mask = word(c, ba + 0x18), b.no_hit = b.hit_mask == -2;
            b.hotkey = static_cast<std::uint32_t>(word(c, ba + 0x38));
            if (!c.ok()) return 3;
            pg.buttons.push_back(b);
        }
        out.push_back(std::move(pg));
    }
    const std::int32_t cur = word(c, desc + 8);
    ic.groups = std::move(out);
    ic.current_group = cur >= 0 && cur < group_count ? cur : -1;
    ic.modal = word(c, desc + 0xC) != 0, ic.mouse = word(c, desc + 0x10) != 0;
    ic.input_mask = static_cast<std::uint32_t>(word(c, desc + 0x14));
    ic.key_map = word(c, desc + 0x18) & 7, ic.select_on_decide = word(c, desc + 0x1C) != 0;
    ic.info = {1, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, 0};
    std::uint32_t k = 0;
    for (auto& g : ic.groups)
        for (std::size_t i = 0; i < g.buttons.size(); ++i, ++k) {
            auto& b = g.buttons[i];
            if (!b.shown || b.bmp_normal == -1) continue;
            b.sprite = button_sprite(id, k);
            show(s, ic, b, picture(g, static_cast<int>(i)));
        }
    return 0;
}

} // namespace

std::uint32_t panel_start(SystemState& s, std::uint32_t id, Ctx& c, std::uint32_t desc) {
    auto it = s.icon_controls.find(id);
    if (it == s.icon_controls.end()) return 1;
    auto& ic = it->second;
    if (ic.basic) return panel_start_basic(s, ic, id, c, desc);
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
        pg.selectable = word(c, ga + 0x14) != 0, pg.hover_selects = word(c, ga + 0x18) != 0;
        pg.click_decides = word(c, ga + 0x1C) != 0, pg.link_id = word(c, ga + 0x20);
        for (std::int32_t i = 0; i < count; ++i) {
            const Word ba = buttons + 0xC4 * static_cast<Word>(i);
            SystemState::PanelButton b;
            b.enabled = word(c, ba) != 0;
            b.shown = word(c, ba + 4) != 0;
            b.x = word(c, ba + 8), b.y = word(c, ba + 0xC);
            b.bmp_normal = word(c, ba + 0x20), b.bmp_focus = word(c, ba + 0x24);
            b.bmp_selected = word(c, ba + 0x28), b.bmp_selected_focus = word(c, ba + 0x2C);
            b.hit_mask = word(c, ba + 0x30), b.no_hit = b.hit_mask == -2;
            b.flags = static_cast<std::uint32_t>(word(c, ba + 0xC0));
            if (!c.ok()) return 3;
            pg.buttons.push_back(b);
        }
        out.push_back(std::move(pg));
    }
    const std::int32_t cur = word(c, desc + 8);
    ic.groups = std::move(out);
    ic.current_group = cur >= 0 && cur < group_count ? cur : -1;
    ic.modal = word(c, desc + 0xC) != 0, ic.input_mask = static_cast<std::uint32_t>(word(c, desc + 0x14));
    ic.key_map = word(c, desc + 0x18) & 7, ic.select_on_decide = word(c, desc + 0x1C) != 0;
    ic.info = {1, 0xFFFFFFFFu, 0xFFFFFFFFu, 0, 0, 0};
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

// Pointer handling, once per scheduler frame (analysis: openbgi docs/panel_system.md, ButtonPanel_Update step 3):
// the button under the pointer changes -> event 0x10000001 (group, index) and the focus picture; a left click on an
// enabled button -> 0x10000007 (click position inside the button) and 0x10000006 (decision), as a sprite panel reports it.
namespace {

enum Action { None, Click, Cancel, Decide, CancelKey, Prev, Next, PrevGroup, NextGroup, Left, Right, Up, Down };
constexpr std::uint32_t kPanelBits[24] = {0x1, 0x2, 0x4, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200, 0x1000, 0x2000, 0x4000,
                                          0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000, 0x400000, 0x800000, 0x1000000, 0x2000000, 0x40000000};
constexpr std::uint32_t kDirPrev = 0xFFFFFFFFu, kDirNext = 1, kDirLeft = 0xFFFF, kDirRight = 1, kDirUp = 0xFFFF0000u, kDirDown = 0x10000;

std::uint32_t pack(int g, int i) { return static_cast<std::uint32_t>(g) << 16 | static_cast<std::uint32_t>(i); }

// the four built-in key maps by key-map number: indexed by the standard-key bit order of kPanelBits
Action mapped_action(int key_map, int k, bool shift) {
    if (k == 0) return Click;
    if (k == 1) return Cancel;
    if (k == 7) return Decide;
    if (k == 8) return CancelKey;
    if (k == 23) return shift ? PrevGroup : NextGroup;
    if (k < 9 || k > 12) return None;
    static const Action maps[4][4] = {{PrevGroup, NextGroup, Prev, Next}, {Prev, Next, PrevGroup, NextGroup}, {Up, Down, Left, Right}, {Left, Right, Up, Down}};
    return maps[key_map & 3][k - 9]; // map 1 and 2 of the original are rows 2 and 3 here (see panel_system.md)
}

// Selects button `i` of group `g` (-1 clears). A button without a normal picture or (sprite panel) a disabled one cannot be selected.
// The linked groups lose their selection. Returns whether the selection changed.
bool select_button(SystemState& s, SystemState::IconControl& ic, std::uint32_t id, int g, int i) {
    if (g < 0 || g >= int(ic.groups.size())) return false;
    auto& grp = ic.groups[std::size_t(g)];
    if (i != -1 && (i < 0 || i >= int(grp.buttons.size()))) return false;
    if (i == grp.selected) return false;
    if (i != -1 && !ic.basic && !grp.buttons[std::size_t(i)].enabled) return false;
    const int old = grp.selected;
    grp.selected = i;
    if (old >= 0) show(s, ic, grp.buttons[std::size_t(old)], picture(grp, old, hovered(ic, g, old)));
    if (i == -1) return true;
    if (grp.buttons[std::size_t(i)].bmp_normal == -1) return false;
    show(s, ic, grp.buttons[std::size_t(i)], picture(grp, i, hovered(ic, g, i)));
    if (grp.link_id != -1)
        for (int h = 0; h < int(ic.groups.size()); ++h)
            if (h != g && ic.groups[std::size_t(h)].link_id == grp.link_id) select_button(s, ic, id, h, -1);
    return true;
}

// Records a decision (-1, -1 for a cancel). A sprite panel reports it as events and goes on; the plain panel ends (started = 0).
void decide(SystemState::IconControl& ic, int g, int i, std::uint32_t flag, std::int32_t cx, std::int32_t cy) {
    if (!ic.basic) {
        if (g >= 0 && i >= 0 && flag) ic.events.push_back({0x10000007u, pack(g, i), static_cast<std::uint32_t>(cy) << 16 | static_cast<std::uint32_t>(cx & 0xFFFF)});
        ic.events.push_back({0x10000006u, g >= 0 && i >= 0 ? pack(g, i) : 0xFFFFFFFFu, flag});
    }
    ic.info = {ic.basic ? 0u : 1u, static_cast<std::uint32_t>(g), static_cast<std::uint32_t>(i), flag, static_cast<std::uint32_t>(cx), static_cast<std::uint32_t>(cy)};
}

bool moved_selection(SystemState& s, SystemState::IconControl& ic, std::uint32_t id, int g, int i, std::uint32_t dir) {
    if (!select_button(s, ic, id, g, i)) return false;
    ic.events.push_back({0x10000004u, pack(g, i), dir});
    return true;
}

} // namespace

void panel_input(SystemState& s) {
    if (!s.display) return;
    const bool trace_panel = std::getenv("OBGI_TRACEPANEL") != nullptr;
    const bool trace_press = s.input_pending[1] || s.input_pending[2] || s.input_pending[3] || !s.wheel_events.empty();
    // A click and a later pointer move can arrive in the same host poll. Hit-test
    // the click at its event position, not at the final position in that batch.
    const int mouse_x = s.input_pending[1] && s.click_position_valid ? s.click_x : s.mouse_x;
    const int mouse_y = s.input_pending[1] && s.click_position_valid ? s.click_y : s.mouse_y;
    // A press is consumed by the first panel that handles it. Visit panels in
    // the same top-to-bottom order as their owning windows, not by control ID.
    std::vector<std::pair<std::uint32_t, SystemState::IconControl*>> panels;
    panels.reserve(s.icon_controls.size());
    for (auto& [id, ic] : s.icon_controls) panels.emplace_back(id, &ic);
    auto priority = [&](const auto& panel) {
        const Sprite* win = s.display->find_sprite(panel.second->window);
        return win ? win->priority : 0u;
    };
    std::stable_sort(panels.begin(), panels.end(), [&](const auto& a, const auto& b) {
        return priority(a) > priority(b);
    });
    for (auto [id, control] : panels) {
        auto& ic = *control;
        if (trace_panel && trace_press)
            std::fprintf(stderr, "PANEL frame-input id=%u basic=%d paint=%d modal=%d info0=%u events=%zu input=%d pending=%d/%d/%d mouse=%d,%d click=%d,%d\n",
                         id, ic.basic, ic.paint_only, ic.modal, ic.info[0], ic.events.size(), s.input_enabled,
                         s.input_pending[1], s.input_pending[2], s.input_pending[3], s.mouse_x, s.mouse_y, mouse_x, mouse_y);
        if (ic.groups.empty() || ic.paint_only || (ic.basic && ic.info[0] == 0)) continue; // not started, painted only, or a plain panel that has decided
        const Sprite* win = s.display->find_sprite(ic.window);
        if (win && (!win->is_shown() || !s.display->windows_shown)) continue;
        // Pointer hit testing covers every group. current_group only controls
        // keyboard navigation; the reference's flat button list has no group filter.
        int hg = -1, hi = -1;
        std::int32_t lx = 0, ly = 0;
        std::int32_t best_priority = std::numeric_limits<std::int32_t>::min();
        int best_order = -1, order = 0;
        for (int gi = 0; gi < int(ic.groups.size()); ++gi) {
            const auto& g = ic.groups[static_cast<std::size_t>(gi)];
            for (std::size_t i = 0; i < g.buttons.size(); ++i, ++order) {
                const auto& b = g.buttons[i];
                if (!b.enabled || b.no_hit || b.sprite == 0) continue;
                const Sprite* sp = s.display->find_sprite(b.sprite);
                const SlotInfo* info = sp ? s.display->slot(sp->slot) : nullptr;
                if (!sp || !sp->visible || !info) continue;
                if (mouse_x >= sp->x && mouse_y >= sp->y && mouse_x < sp->x + std::int32_t(info->width) &&
                    mouse_y < sp->y + std::int32_t(info->height)) {
                    const std::uint32_t px = static_cast<std::uint32_t>(mouse_x - sp->x);
                    const std::uint32_t py = static_cast<std::uint32_t>(mouse_y - sp->y);
                    if (sp->has_hit_mask) {
                        if (px >= sp->hit_w || py >= sp->hit_h) continue;
                        const std::size_t pitch = (std::size_t{sp->hit_w} + 7) >> 3;
                        if (((sp->hit_bits[std::size_t{py} * pitch + (px >> 3)] >> (px & 7)) & 1) == 0) continue;
                    }
                    // Window sub-sprites use the button's y as their local sort
                    // priority when flag 0x02 is set. Equal layers are inserted
                    // newest-first, so the later descriptor entry wins a tie.
                    const std::int32_t layer_priority = (b.flags & 0x02) ? b.y : 0;
                    if (layer_priority < best_priority || (layer_priority == best_priority && order < best_order)) continue;
                    best_priority = layer_priority;
                    best_order = order;
                    hg = gi, hi = int(i);
                    lx = static_cast<std::int32_t>(px), ly = static_cast<std::int32_t>(py);
                }
            }
        }
        if (std::getenv("OBGI_DBGPANEL") && (hg != ic.hover_group || hi != ic.hover_index))
            std::fprintf(stderr, "panel %u pointer %d,%d hover %d/%d group %d\n", id, s.mouse_x, s.mouse_y, hg, hi, ic.current_group);
        if (hg != ic.hover_group || hi != ic.hover_index) {
            const int og = ic.hover_group, oi = ic.hover_index;
            ic.hover_group = hg, ic.hover_index = hi;
            ic.events.push_back({0x10000001u, static_cast<std::uint32_t>(hg), static_cast<std::uint32_t>(hi)});
            if (og >= 0 && oi >= 0) show(s, ic, ic.groups[std::size_t(og)].buttons[std::size_t(oi)], picture(ic.groups[std::size_t(og)], oi));
            if (hi >= 0) {
                const auto& g = ic.groups[std::size_t(hg)];
                show(s, ic, g.buttons[std::size_t(hi)], picture(g, hi, true));
                ic.events.push_back({0x10000002u, pack(hg, hi), g.buttons[std::size_t(hi)].bmp_focus != -1});
            }
        }
        // hover selection: after the pointer moved, the button under it becomes its group's selection
        if (hi >= 0 && ic.groups[std::size_t(hg)].hover_selects && (s.mouse_x != ic.last_mouse_x || s.mouse_y != ic.last_mouse_y)) {
            auto& grp = ic.groups[std::size_t(hg)];
            if (grp.selectable) ic.current_group = hg;
            if (select_button(s, ic, id, hg, hi)) ic.events.push_back({0x10000004u, pack(hg, hi), 0});
        }
        ic.last_mouse_x = s.mouse_x, ic.last_mouse_y = s.mouse_y;

        // what was pressed since the last update, as logical input bits (a non-modal panel only reacts to the mouse buttons,
        // the wheel and the cancel key)
        const bool pointer_layer = ic.modal || hi >= 0 || (ic.mouse && win && sprite_hit(*s.display, ic.window, mouse_x, mouse_y));
        const std::uint32_t win_key = ((win ? win->priority : 0u) << 20) | 0xFFFFFu;
        const bool key_layer = !ic.modal || (s.input_enabled && !s.key_layers.empty() && win_key >= s.key_layers.front());
        std::uint32_t bits = 0;
        std::array<bool, 256> pressed_now{};
        if (s.input_enabled && pointer_layer && key_layer) {
            for (std::uint32_t bit : kPanelBits) {
                // A non-modal panel polls only the mouse, wheel and cancel groups
                // (0x2C3). Do not consume navigation keys that it will discard.
                if (!ic.modal && (bit & 0x2C3u) == 0) continue;
                for (std::uint32_t n : s.input_numbers(bit))
                    if (consume_input_trigger(s, n)) {
                        bits |= bit;
                        if (n < pressed_now.size()) pressed_now[n] = true;
                        break;
                    }
                }
        }
        const std::uint32_t raw_bits = bits;
        const bool shift = false; // the host does not report modifier keys; Tab goes to the next group
        bits &= ~ic.input_mask;
        if (!ic.modal) bits &= 0x2C3;
        if (trace_panel && trace_press)
            std::fprintf(stderr, "PANEL hit id=%u hover=%d/%d ptrlayer=%d keylayer=%d winprio=%u winkey=%u keytop=%u rawbits=%X filtered=%X active=%d\n",
                         id, hg, hi, pointer_layer, key_layer, win ? win->priority : 0u, win_key,
                         s.key_layers.empty() ? 0u : s.key_layers.front(), raw_bits, bits, s.input_enabled);
        auto was_pressed = [&](std::uint32_t vk) {
            return vk < pressed_now.size() && (pressed_now[vk] ||
                (s.input_enabled && pointer_layer && key_layer && consume_input_trigger(s, vk)));
        };

        Action act = None;
        int k = 0;
        for (; k < 24; ++k)
            if (bits & kPanelBits[k]) { act = mapped_action(ic.key_map, k, shift); break; }
        const int g = ic.current_group;
        const int sel = g >= 0 ? ic.groups[std::size_t(g)].selected : -1;
        auto cycle_buttons = [&](int dir) {
            if (g < 0) return;
            const int n = int(ic.groups[std::size_t(g)].buttons.size());
            int idx = sel;
            for (int t = 0; t < n; ++t) {
                idx = dir < 0 ? (idx - 1 < 0 || idx - 1 >= n ? n - 1 : idx - 1) : (idx + 1 < 0 || idx + 1 >= n ? 0 : idx + 1);
                if (moved_selection(s, ic, id, g, idx, dir < 0 ? kDirPrev : kDirNext)) return;
            }
        };
        auto cycle_groups = [&](int dir) {
            const int n = int(ic.groups.size());
            int gi = g;
            for (int t = 0; t < n; ++t) {
                gi = dir < 0 ? (gi - 1 < 0 || gi - 1 >= n ? n - 1 : gi - 1) : (gi + 1 < 0 || gi + 1 >= n ? 0 : gi + 1);
                if (ic.groups[std::size_t(gi)].selectable) {
                    ic.current_group = gi;
                    ic.events.push_back({0x10000003u, static_cast<std::uint32_t>(gi), dir < 0 ? kDirPrev : kDirNext});
                    return;
                }
            }
        };
        auto grid_move = [&](bool horizontal, int dir) {
            if (g < 0) return;
            const auto& grp = ic.groups[std::size_t(g)];
            const int n = int(grp.buttons.size()), cols = std::max(grp.columns, 1), rows = (n + cols - 1) / cols;
            int idx = sel < 0 ? 0 : sel;
            for (int t = 0, lim = horizontal ? cols : rows; t < lim; ++t) {
                if (horizontal) {
                    int col = idx % cols + dir;
                    col = col < 0 ? cols - 1 : (col >= cols ? 0 : col);
                    idx = idx / cols * cols + col;
                } else {
                    int row = idx / cols + dir;
                    row = row < 0 ? rows - 1 : (row >= rows ? 0 : row);
                    idx = row * cols + idx % cols;
                }
                const std::uint32_t d = horizontal ? (dir < 0 ? kDirLeft : kDirRight) : (dir < 0 ? kDirUp : kDirDown);
                if (idx < n && moved_selection(s, ic, id, g, idx, d)) return;
            }
        };
        switch (act) {
        case Click:
            if (trace_panel) std::fprintf(stderr, "PANEL action id=%u CLICK hit=%d/%d pos=%d,%d\n", id, hg, hi, mouse_x, mouse_y);
            if (hi < 0) {
                if (!ic.basic) ic.events.push_back({0x10000007u, 0xFFFFFFFFu, static_cast<std::uint32_t>(mouse_y - (win ? win->y : 0)) << 16 | (static_cast<std::uint32_t>(mouse_x - (win ? win->x : 0)) & 0xFFFF)});
                break;
            }
            // Confirming a modal over a pending in-game message is also the user's
            // explicit resume action. Replay only the affirmative second button of
            // the two-choice modal; the panel still owns and consumes the physical click.
            if (ic.modal && hi == 1 && ic.groups[std::size_t(hg)].buttons.size() == 2 && s.message_wait_clicks) {
                ++s.message_input_replay_serial;
                if (trace_panel) std::fprintf(stderr, "PANEL replay modal-confirm click to pending message id=%u group=%d button=%d\n", id, hg, hi);
            }
            decide(ic, hg, hi, 1, lx, ly);
            if (ic.select_on_decide) select_button(s, ic, id, hg, hi);
            if (!ic.basic && ic.groups[std::size_t(hg)].selectable) ic.current_group = hg;
            break;
        case Cancel: decide(ic, -1, -1, 1, 0, 0); break;
        case CancelKey: decide(ic, -1, -1, 0, 0, 0); break;
        case Decide:
            if (g >= 0 && sel >= 0) decide(ic, g, sel, 0, 0, 0);
            break;
        case Prev: cycle_buttons(-1); break;
        case Next: cycle_buttons(1); break;
        case PrevGroup: cycle_groups(-1); break;
        case NextGroup: cycle_groups(1); break;
        case Left: grid_move(true, -1); break;
        case Right: grid_move(true, 1); break;
        case Up: grid_move(false, -1); break;
        case Down: grid_move(false, 1); break;
        case None:
            // a pressed hot key decides its button at once (modal panels)
            if (ic.modal)
                for (int gi = 0; gi < int(ic.groups.size()); ++gi)
                    for (int bi = 0; bi < int(ic.groups[std::size_t(gi)].buttons.size()); ++bi) {
                        const auto& b = ic.groups[std::size_t(gi)].buttons[std::size_t(bi)];
                        if (b.sprite != 0 && (b.hotkey & 0x7FFFFFFF) != 0 && was_pressed(b.hotkey & 0x7FFFFFFF)) {
                            decide(ic, gi, bi, 0, 0, 0);
                            gi = int(ic.groups.size());
                            break;
                        }
                    }
            break;
        }
        while (ic.events.size() > 256) ic.events.pop_front(); // nobody is draining them
    }
}

// 90:B6 / 90:B7: paint the buttons of a descriptor over a window without a running panel. The buttons are kept as a panel that
// takes no input (id 0x80000000 | window), replaced by the next paint of the same window. Returns 0 ok, 1 no such window, 2 / 3 as
// panel_start.
std::uint32_t panel_paint(SystemState& s, Ctx& c, std::uint32_t window, std::uint32_t desc, bool basic) {
    if (!s.display || !s.display->find_sprite(window)) return 1;
    // Painting replaces the window's existing sub-sprites, including those
    // created by a running panel. Keep the controller itself alive until the
    // script stops it, but detach its old visual items from the window.
    for (auto& [other_id, other] : s.icon_controls) {
        if (other.window != window) continue;
        for (auto& group : other.groups)
            for (auto& button : group.buttons)
                if (button.sprite) {
                    s.display->remove_sprite(button.sprite);
                    button.sprite = 0;
                }
    }
    const std::uint32_t id = 0x80000000u | (window & 0x7FFFFFFFu);
    panel_stop(s, id);
    auto& ic = s.icon_controls[id];
    ic = {};
    ic.window = window, ic.basic = basic, ic.paint_only = true;
    return panel_start(s, id, c, desc);
}

// 80:AC messages to a panel (analysis: openbgi docs/panel_system.md): 0x10000000 decide a button, 0x10000001 set the current group,
// 0x10000002 select a button.
void panel_select(SystemState& s, std::uint32_t id, int group, int index) {
    auto it = s.icon_controls.find(id);
    if (it != s.icon_controls.end()) select_button(s, it->second, id, group, index);
}

void panel_decide(SystemState& s, std::uint32_t id, int group, int index, std::uint32_t flag) {
    auto it = s.icon_controls.find(id);
    if (it == s.icon_controls.end() || it->second.groups.empty()) return;
    if (std::getenv("OBGI_TRACEPANEL")) std::fprintf(stderr, "PANEL script-decide id=%u group=%d index=%d flag=%u\n", id, group, index, flag);
    decide(it->second, group, index, flag, 0, 0);
}

void panel_set_group(SystemState& s, std::uint32_t id, int group) {
    auto it = s.icon_controls.find(id);
    if (it == s.icon_controls.end() || group < 0 || group >= int(it->second.groups.size())) return;
    if (it->second.groups[std::size_t(group)].selectable) it->second.current_group = group;
}

void panel_sync(SystemState& s) {
    if (!s.display) return;
    for (auto& [id, ic] : s.icon_controls) {
        const Sprite* win = s.display->find_sprite(ic.window);
        const auto [ox, oy] = panel_offset(s, ic);
        for (auto& g : ic.groups)
            for (std::size_t i = 0; i < g.buttons.size(); ++i) {
                auto& b = g.buttons[i];
                if (b.sprite == 0) continue;
                Sprite* sp = s.display->find_sprite(b.sprite);
                if (!sp) continue;
                sp->x = (win ? win->x : 0) + ox + b.x;
                sp->y = (win ? win->y : 0) + oy + b.y;
                sp->priority = (win ? win->priority : 0) + 1;
                sp->alpha = win ? win->alpha : 0;
                sp->fade = win ? win->fade : 0, sp->opacity = win ? win->opacity : 0x100;
                sp->visible = win && win->is_shown() && s.display->windows_shown && s.display->slot(sp->slot) != nullptr;
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
