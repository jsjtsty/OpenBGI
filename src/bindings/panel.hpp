#pragma once
#include <cstdint>

#include "bindings/system_subops.hpp"

namespace obgi::vm { struct Ctx; }

namespace obgi {

// Sprite panels (91:B8 / 91:BA, analysis/精灵面板.md): the title menu and the like. A panel is a set of groups of
// buttons; each button is an image drawn at a fixed place over a window object.
// start: reads the script's descriptor and draws the buttons. Returns the result code of 91:BA
// (0 ok, 2 the descriptor or its tables are unusable, 3 a button table is unusable).
std::uint32_t panel_start(SystemState& state, std::uint32_t id, vm::Ctx& c, std::uint32_t desc_addr);
// 90:B6 (basic) / 90:B7: paints the buttons of a descriptor over a window object without a running panel. 0 ok, 1 no such window.
std::uint32_t panel_paint(SystemState& state, vm::Ctx& c, std::uint32_t window, std::uint32_t desc_addr, bool basic);
// Moves the selection of a group to a button (-1 clears it) and redraws the two buttons concerned.
void panel_select(SystemState& state, std::uint32_t id, int group, int index);
// 80:AC messages: decide a button ((-1, -1) is a cancel), set the current group.
void panel_decide(SystemState& state, std::uint32_t id, int group, int index, std::uint32_t flag);
void panel_set_group(SystemState& state, std::uint32_t id, int group);
// Once per frame: the buttons follow their window object (place, draw order, transparency, visibility).
void panel_sync(SystemState& state);
// Once per frame: pointer hover and left clicks on the current group's buttons become panel events (90:BF).
void panel_input(SystemState& state);
// Stops a controller; preserve_picture leaves its buttons owned by the window without input.
void panel_stop(SystemState& state, std::uint32_t id, bool preserve_picture = false);

} // namespace obgi
