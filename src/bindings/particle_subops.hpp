#pragma once
#include "bindings/system_subops.hpp"
#include "render/display.hpp"
#include "vm/vm.hpp"

namespace obgi {

// C0:00..2D: particle screens (the reference engine's particle engine: two particle types, patterns, wind, camera). Hooks its
// simulation into SystemState::per_frame.
void register_particle_subops(vm::SubopRegistry& registry, SystemState& state, Display* display);

} // namespace obgi
