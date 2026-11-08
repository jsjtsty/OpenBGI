#include "text/layout.hpp"

#include <algorithm>

namespace obgi {

TextLayout layout_text(Font& font, const std::u32string& text, const TextStyle& style, std::uint32_t max_width) {
    TextLayout out;
    int pen = style.first_indent, line = 0, widest = 0;
    auto end_line = [&] {
        widest = std::max(widest, pen);
        pen = style.continuation_indent;
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
        const bool half = cp < 0x80 || (cp >= 0xFF61 && cp <= 0xFF9F);
        const int advance = style.fixed_advance > 0 ? (half ? style.fixed_advance / 2 : style.fixed_advance) : g->advance;
        if (style.wrap && max_width && pen > style.continuation_indent && pen + advance > static_cast<int>(max_width) - style.right_margin) end_line();
        out.glyphs.push_back({*g, pen, line});
        pen += advance;
    }
    widest = std::max(widest, pen);
    out.lines = line + (pen > 0 || out.glyphs.empty() ? 1 : 0);
    out.last_width = pen;
    out.row_height = style.row_height > 0 ? style.row_height : font.line_height() + style.line_gap;
    out.ascent = style.baseline >= 0 ? style.baseline : font.ascent();
    // glyph ink can reach past the advance box (italic overhang) or above the line: size to the box, clip the rest; a shadow adds
    // its offset on the right and at the bottom
    out.width = static_cast<std::uint32_t>(std::max(widest + std::max(style.shadow_dx, 0), 1));
    out.height = static_cast<std::uint32_t>(std::max((out.lines - 1) * out.row_height + font.line_height() + std::max(style.shadow_dy, 0), 1));
    return out;
}

TextImage paint_text(const TextLayout& layout, const TextStyle& style, std::size_t count, const std::vector<std::uint8_t>& opacity) {
    TextImage img;
    img.lines = layout.lines;
    img.last_width = layout.last_width;
    img.width = layout.width;
    img.height = layout.height;
    img.pixels.assign(std::size_t(img.width) * img.height, 0);

    // straight-alpha "over": the source colour at coverage `a` (0..255) onto the pixel
    auto over = [&](int x, int y, std::uint32_t rgb, std::uint32_t a) {
        if (!a || x < 0 || y < 0 || x >= static_cast<int>(img.width) || y >= static_cast<int>(img.height)) return;
        std::uint32_t& dst = img.pixels[std::size_t(y) * img.width + x];
        const std::uint32_t da = dst >> 24;
        const std::uint32_t ra = a + da * (255 - a) / 255;
        if (!ra) return;
        auto mix = [&](std::uint32_t s, std::uint32_t d) { return (s * a * 255 + d * da * (255 - a) + ra * 127) / (ra * 255); };
        const std::uint32_t r = mix(rgb >> 16 & 255, dst >> 16 & 255), g = mix(rgb >> 8 & 255, dst >> 8 & 255), b = mix(rgb & 255, dst & 255);
        dst = b | g << 8 | r << 16 | ra << 24;
    };
    const bool shadow = style.shadow_dx > 0 || style.shadow_dy > 0;
    const std::uint32_t rgb = style.color & 0xFFFFFF, shadow_rgb = style.shadow_color & 0xFFFFFF;
    const std::size_t n = std::min(count, layout.glyphs.size());
    for (std::size_t i = 0; i < n; ++i) {
        const PlacedGlyph& p = layout.glyphs[i];
        const std::uint32_t o = i < opacity.size() ? opacity[i] : 255;
        if (!o) continue;
        const int base_y = p.line * layout.row_height + layout.ascent;
        for (int pass = shadow ? 0 : 1; pass < 2; ++pass) {
            const int ox = pass == 0 ? style.shadow_dx : 0, oy = pass == 0 ? style.shadow_dy : 0;
            for (int gy = 0; gy < p.glyph.height; ++gy)
                for (int gx = 0; gx < p.glyph.width; ++gx) {
                    std::uint32_t a = p.glyph.coverage[std::size_t(gy) * p.glyph.width + gx];
                    if (pass == 0) a = a * std::clamp(style.shadow_level, 0, 0x100) >> 8;
                    over(p.x + p.glyph.left + gx + ox, base_y - p.glyph.top + gy + oy, pass == 0 ? shadow_rgb : rgb, a * o / 255);
                }
        }
    }
    return img;
}

Result<TextImage> render_text(Font& font, const std::u32string& text, const TextStyle& style, std::uint32_t max_width) {
    const TextLayout layout = layout_text(font, text, style, max_width);
    return paint_text(layout, style, layout.glyphs.size());
}

} // namespace obgi
