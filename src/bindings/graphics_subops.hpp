#pragma once
#include "bindings/arc_image_source.hpp"
#include "render/display.hpp"
#include "vm/vm.hpp"

namespace obgi {

// What the graphics sub-opcodes (main opcode 0x90) act on. Both must outlive the registry's use.
struct GraphicsHost {
    Display* display = nullptr;
    ArcImageSource* images = nullptr;
};

// Image slots (90:10..90:1F) and display objects (90:30..90:57). Stack effects and argument order follow
// analysis/子opcode语义表.md and 显示对象属性.md; ops not listed there are left unregistered (hard error).
void register_graphics_subops(vm::SubopRegistry& registry, GraphicsHost& host);

} // namespace obgi
