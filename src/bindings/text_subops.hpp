#pragma once
#include "render/display.hpp"
#include "text/font.hpp"
#include "vm/vm.hpp"
#include "services/message.hpp"

namespace obgi {

struct SystemState;

struct TextHost {
    Display* display = nullptr;
    FontLibrary* fonts = nullptr;
    const std::uint32_t* code_page = nullptr; // 81:00: 932 or 65001 (UTF-8) for the scripts' strings
    const SystemState* system = nullptr; // 91:98 text layout settings
    const std::vector<std::string>* font_ids = nullptr; // B0:C1: font names by the id the scripts hold (92:1E)
    std::shared_ptr<MessageService> messages = std::make_shared<MessageService>();
};

// 92:9C draws a string into an image slot (analysis/文字绘制.md). Supported: font name and size, colour,
// line gap, "\n" and wrapping at the slot edge. Not yet: vertical text, ruby, text effects (mode 1/2),
// tags; a script asking for vertical text is a hard error, ruby annotations and effect arguments are accepted but not drawn.
void register_text_subops(vm::SubopRegistry& registry, TextHost& host);

} // namespace obgi
