#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "text/font.hpp"

namespace obgi {

struct TextStyle {
    std::uint32_t color = 0xFFFFFF; // 0xRRGGBB
    int line_gap = 0;               // extra pixels between lines
    bool wrap = true;               // break lines at max_width
};

// Straight-alpha BGRA pixels (one packed uint32 each, B | G<<8 | R<<16 | A<<24) of a block of text on a
// transparent background, ready to be blended into an image slot.
struct TextImage {
    std::uint32_t width = 0, height = 0;
    int lines = 0;
    std::vector<std::uint32_t> pixels;
};

// Lays out `text` (code points) with a plain greedy line breaker: '\n' ends a line, other control
// characters are skipped, and with style.wrap a line is broken before the glyph that would pass
// max_width (0 = unlimited). The script engine's full rules (ruby, kinsoku, tags) are analysed in
// analysis/排版引擎.md but not implemented here yet.
Result<TextImage> render_text(Font& font, const std::u32string& text, const TextStyle& style, std::uint32_t max_width);

} // namespace obgi
