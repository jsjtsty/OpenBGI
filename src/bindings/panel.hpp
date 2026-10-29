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
// Moves the selection of a group to a button (-1 clears it) and redraws the two buttons concerned.
void panel_select(SystemState& state, std::uint32_t id, int group, int index);
// Once per frame: the buttons follow their window object (place, draw order, transparency, visibility).
void panel_sync(SystemState& state);
// Removes the button sprites of the panel.
void panel_stop(SystemState& state, std::uint32_t id);

} // namespace obgi
