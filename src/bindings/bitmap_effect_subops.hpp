#pragma once
#include "render/display.hpp"
#include "vm/vm.hpp"

namespace obgi {

// 90:CC / 90:CD tone curves, 92:18 bitmap to gray, 92:19 invert a gray bitmap (ported from the reference engine's tonecurve.c / blit_filter.c).
void register_bitmap_effect_subops(vm::SubopRegistry& registry, Display* display);

} // namespace obgi
