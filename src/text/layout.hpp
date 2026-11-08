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
    int first_indent = 0;           // pixels the first line starts in (text continuing after earlier text on the line)
    int continuation_indent = 0;
    int fixed_advance = 0;          // full-width character advance, or 0 to use font metrics
    int row_height = 0;             // explicit line height, or 0 to use font metrics
    int baseline = -1;              // explicit baseline from line top, or -1 to use font metrics
    int right_margin = 0;
    // A shadow under every glyph: the glyph again, (shadow_dx, shadow_dy) pixels to the lower right, drawn first. Colour 0 is
    // the dimmed silhouette of the glyph (black); the level is its opacity, 0x100 = opaque. (0, 0) = no shadow.
    int shadow_dx = 0, shadow_dy = 0;
    std::uint32_t shadow_color = 0;
    int shadow_level = 0x100;
};

// Straight-alpha BGRA pixels (one packed uint32 each, B | G<<8 | R<<16 | A<<24) of a block of text on a
// transparent background, ready to be blended into an image slot.
struct TextImage {
    std::uint32_t width = 0, height = 0;
    int lines = 0;
    int last_width = 0; // pen position at the end of the last line (includes first_indent when there is one line)
    std::vector<std::uint32_t> pixels;
};

// A text laid out once; paint_text() draws it as often as needed with different per-glyph opacities.
struct PlacedGlyph {
    Glyph glyph;
    int x = 0, line = 0;
};
struct TextLayout {
    std::vector<PlacedGlyph> glyphs; // in text order; control characters have none
    int lines = 0, last_width = 0, row_height = 0, ascent = 0;
    std::uint32_t width = 1, height = 1;
};

// Lays out `text` (code points) with a plain greedy line breaker: '\n' ends a line, other control
// characters are skipped, and with style.wrap a line is broken before the glyph that would pass
// max_width (0 = unlimited). The script engine's full rules (ruby, kinsoku, tags) are analysed in
// analysis/排版引擎.md but not implemented here yet.
TextLayout layout_text(Font& font, const std::u32string& text, const TextStyle& style, std::uint32_t max_width);

// Draws the first `count` glyphs of a layout. `opacity` (0..255 per glyph, empty = all opaque) scales a glyph and its shadow.
TextImage paint_text(const TextLayout& layout, const TextStyle& style, std::size_t count, const std::vector<std::uint8_t>& opacity = {});

Result<TextImage> render_text(Font& font, const std::u32string& text, const TextStyle& style, std::uint32_t max_width);

} // namespace obgi
