#include "text/layout.hpp"

#include <algorithm>

namespace obgi {

namespace {

struct Placed {
    const Glyph* glyph;
    int x, line;
};

} // namespace

Result<TextImage> render_text(Font& font, const std::u32string& text, const TextStyle& style, std::uint32_t max_width) {
    std::vector<Placed> placed;
    int pen = style.first_indent, line = 0, widest = 0;
    auto end_line = [&] {
        widest = std::max(widest, pen);
        pen = 0;
        ++line;
    };
    for (char32_t cp : text) {
        if (cp == U'\n') {
            end_line();
            continue;
        }
        if (cp < 0x20) continue;
        const Glyph* g = font.glyph(cp);
        if (!g) g = font.glyph(U'?'); // missing glyph: the original shows a placeholder as well
        if (!g) continue;
        if (style.wrap && max_width && pen > 0 && pen + g->advance > static_cast<int>(max_width)) end_line();
        placed.push_back({g, pen, line});
        pen += g->advance;
    }
    widest = std::max(widest, pen);
    const int lines = line + (pen > 0 || placed.empty() ? 1 : 0);

    const int row_height = font.line_height() + style.line_gap;
    TextImage img;
    img.lines = lines;
    img.last_width = pen;
    img.width = static_cast<std::uint32_t>(std::max(widest, 1));
    img.height = static_cast<std::uint32_t>(std::max(lines * row_height - style.line_gap, 1));
    // glyph ink can reach past the advance box (italic overhang) or above the line: size to the box, clip the rest
    img.pixels.assign(std::size_t(img.width) * img.height, 0);

    const std::uint32_t rgb = style.color & 0xFFFFFF;
    const std::uint32_t r = rgb >> 16 & 255, gch = rgb >> 8 & 255, b = rgb & 255;
    for (const Placed& p : placed) {
        const int base_y = p.line * row_height + font.ascent();
        for (int gy = 0; gy < p.glyph->height; ++gy) {
            const int y = base_y - p.glyph->top + gy;
            if (y < 0 || y >= static_cast<int>(img.height)) continue;
            for (int gx = 0; gx < p.glyph->width; ++gx) {
                const int x = p.x + p.glyph->left + gx;
                if (x < 0 || x >= static_cast<int>(img.width)) continue;
                const std::uint32_t a = p.glyph->coverage[std::size_t(gy) * p.glyph->width + gx];
                if (!a) continue;
                std::uint32_t& dst = img.pixels[std::size_t(y) * img.width + x];
                // overlapping glyphs (rare) keep the stronger coverage
                if (a > (dst >> 24)) dst = b | gch << 8 | r << 16 | a << 24;
            }
        }
    }
    return img;
}

} // namespace obgi
