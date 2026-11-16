#pragma once
#include "bindings/arc_resource_finder.hpp"
#include "render/display.hpp"
#include "vm/vm.hpp"
#include "services/sequence.hpp"

namespace obgi {

// 90:F4..F7 and 92:F1: BF_Movie frame sequences decoded into image slots.
void register_sequence_subops(vm::SubopRegistry& registry, SequenceService& sequences, ArcResourceFinder& resources);

} // namespace obgi
