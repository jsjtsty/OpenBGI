#pragma once
#include "render/display.hpp"
#include "text/font.hpp"
#include "vm/vm.hpp"

namespace obgi {

struct TextHost {
    Display* display = nullptr;
    FontLibrary* fonts = nullptr;
};

// 92:9C draws a string into an image slot (analysis/文字绘制.md). Supported: font name and size, colour,
// line gap, "\n" and wrapping at the slot edge. Not yet: vertical text, ruby, text effects (mode 1/2),
// tags; a script asking for vertical text is a hard error, ruby/effect arguments are accepted and ignored.
void register_text_subops(vm::SubopRegistry& registry, TextHost& host);

} // namespace obgi
