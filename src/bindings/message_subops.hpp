#pragma once
#include "bindings/font_subops.hpp"
#include "bindings/system_subops.hpp"
#include "bindings/text_subops.hpp"

namespace obgi {

// The message window (the window objects of 90:80..89) and the text written into it: 90:86 sets the frame picture, 90:90 /
// 91:90 / 92:90 write a string (waiting for a click at its end when asked) and 92:8E clears the text. The window's picture
// is the frame layer with the text layer over it, composed into the object's image slot. Registered after the plane ops,
// which it overrides for the ones it models.
void register_message_subops(vm::SubopRegistry& registry, SystemState& state, TextHost& text, FontHost& fonts);

} // namespace obgi
