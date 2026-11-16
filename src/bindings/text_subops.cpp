#include "bindings/text_subops.hpp"
#include "bindings/system_subops.hpp"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>

#include "text/encoding.hpp"
#include "text/layout.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

// Slot id used for the temporary glyph image; scripts only address 0..0x3fff.
constexpr std::uint32_t kTextSlot = 0x7FFF0000;

} // namespace

void register_text_subops(vm::SubopRegistry& r, TextHost& host) {
    // 92:1E (slot, x, y, text, font id, size, bold, a, b) -> width: draws one text into an image slot with a registered font (the
    // reading of the arguments follows FUN_00494b80 / FUN_00404160 of the game: the font is found by its id in the B0:C1 table).
    // Colour and the last two arguments (always 0 in the game's scripts) are not known: white text is drawn.
    r.add({0x921E, "draw text with font id", 9, 1, [&host](Ctx& c) {
               Word b = c.pop(), a = c.pop(), bold = c.pop(), size = c.pop(), font_id = c.pop();
               (void)a, (void)b, (void)bold;
               std::string text = c.cstring(c.pop());
               auto y = static_cast<std::int32_t>(c.pop());
               auto x = static_cast<std::int32_t>(c.pop());
               Word slot = c.pop();
               if (!c.ok()) return 0;
               const SlotInfo* info = host.display->slot(slot);
               if (!info) { c.fail(Errc::Range, "no image slot " + std::to_string(slot)); return 0; }
               if (!host.font_ids || font_id >= host.font_ids->size()) { c.fail(Errc::Range, "no font " + std::to_string(font_id)); return 0; }
               auto font = host.fonts->open((*host.font_ids)[font_id], static_cast<int>(size));
               if (!font) { c.fail(font.error().code, font.error().message); return 0; }
               TextStyle style;
               style.wrap = false;
               const std::int64_t room = std::int64_t{info->width} - x;
               auto img = render_text(**font, script_to_utf32(text, host.code_page ? *host.code_page : 932), style, room > 0 ? static_cast<std::uint32_t>(room) : 1);
               if (!img) { c.fail(img.error().code, img.error().message); return 0; }
               if (auto up = host.display->upload_slot(kTextSlot, img->width, img->height, SlotFormat::Bgra, img->pixels); !up) {
                   c.fail(up.error().code, up.error().message);
                   return 0;
               }
               auto blit = host.display->blit(slot, x, y, kTextSlot, 0, 0, img->width, img->height, 0, 0x100);
               host.display->free_slot(kTextSlot);
               if (!blit) c.fail(blit.error().code, blit.error().message);
               else c.push(img->width);
               return 0;
           }});
    // 91:9D is the older variant: a font ID replaces the font-name address,
    // and one ruby argument is used for both native ruby parameters.
    auto draw_text = [&host](Ctx& c, bool legacy) {
               c.pop(); // legacy effect flag, or 92:9C reserved argument
               if (!legacy) for (int i = 0; i < 5; ++i) c.pop(); // effects are not rendered yet
               Word ruby_enabled = 0;
               Word color = 0;
               if (legacy) color = c.pop(); // 91:9D puts text color at the top
               auto line_gap = static_cast<std::int32_t>(c.pop());
               Word hang = c.pop(), prop = c.pop();
               (void)hang; (void)prop;
               c.pop(); c.pop(); // remaining font parameters
               Word size = c.pop();
               std::string font_name;
               if (legacy) {
                   const Word id = c.pop();
                   if (c.ok() && (!host.font_ids || id >= host.font_ids->size())) {
                       c.fail(Errc::Range, "no font " + std::to_string(id));
                       return 0;
                   }
                   if (c.ok()) font_name = (*host.font_ids)[id];
                   c.pop(); // ruby dictionary
                   ruby_enabled = c.pop();
               } else {
                   font_name = c.cstring(c.pop());
                   c.pop(); c.pop(); // ruby parameters and dictionary
                   ruby_enabled = c.pop();
                   color = c.pop();
               }
               std::string text = c.cstring(c.pop());
               auto y = static_cast<std::int32_t>(c.pop());
               auto x = static_cast<std::int32_t>(c.pop());
               Word slot = c.pop();
               if (!c.ok()) return 0;

               const SlotInfo* info = host.display->slot(slot);
               if (!info) {
                   c.fail(Errc::Range, "no image slot " + std::to_string(slot));
                   return 0;
               }
               if (std::getenv("OBGI_DBGTEXT"))
                   std::fprintf(stderr, "92:9C slot %u at %d,%d size %u prop %u ruby %u str=%s\n", slot, x, y, size, prop, ruby_enabled, text.c_str());

               auto font = host.fonts->open(font_name, static_cast<int>(size));
               if (!font) {
                   c.fail(font.error().code, font.error().message);
                   return 0;
               }
               TextStyle style;
               style.color = color;
               // 92:9C specifies line spacing as a percentage of the font size.
               style.line_gap = static_cast<int>(std::int64_t{size} * line_gap / 100);
               style.row_height = std::max(static_cast<int>(size) + style.line_gap, 1);
               // Font ascent is the baseline in the font's own metrics, as in
               // the original's GDI glyph placement; a fixed 7/8 em can sit too high.
               style.baseline = (*font)->ascent();
               auto draw_x = x;
               if (ruby_enabled) {
                   int indent = 0, ruby_percent = 40; // engine defaults before 91:98
                   if (host.system) {
                       auto it = host.system->records.find(0x9198);
                       if (it != host.system->records.end() && it->second.size() >= 5) {
                           ruby_percent = static_cast<int>(it->second[3]);
                           indent = static_cast<int>(it->second[4]);
                       }
                   }
                   // Ruby indent is relative to the destination slot, including
                   // after wraps. Render from x=0 so later lines can begin left
                   // of a nonzero initial cursor without clipping the temporary.
                   style.first_indent = x == 0 ? indent : x;
                   style.continuation_indent = indent;
                   draw_x = 0;
                   y += static_cast<std::int32_t>(std::int64_t{size} * ruby_percent / 100);
               }
               const std::int64_t room = std::int64_t{info->width} - draw_x;
               const auto decoded = script_to_utf32(text, host.code_page ? *host.code_page : 932);
               const auto max_width = room > 0 ? static_cast<std::uint32_t>(room) : 1;
               // A replacement font can have taller metrics than the Windows font
               // used to size the script's bitmap. Fit a single line's ink into
               // the available height when the whole line can fit by moving up.
               const auto layout = layout_text(**font, decoded, style, max_width);
               if (layout.lines == 1 && !layout.glyphs.empty()) {
                   int top = INT_MAX, bottom = INT_MIN;
                   for (const auto& placed : layout.glyphs) {
                       if (!placed.glyph.width || !placed.glyph.height) continue;
                       const int glyph_top = style.baseline - placed.glyph.top;
                       top = std::min(top, glyph_top);
                       bottom = std::max(bottom, glyph_top + placed.glyph.height);
                   }
                   if (top != INT_MAX) {
                       // Keep room for the one-pixel offset used by script-side shadows.
                       const auto overflow = std::int64_t{bottom} - (std::int64_t{info->height} - std::max(y, 2));
                       if (overflow > 0 && overflow <= top) style.baseline -= static_cast<int>(overflow);
                   }
               }
               auto img = render_text(**font, decoded, style, max_width);
               if (!img) {
                   c.fail(img.error().code, img.error().message);
                   return 0;
               }
               if (auto up = host.display->upload_slot(kTextSlot, img->width, img->height, SlotFormat::Bgra, img->pixels); !up) {
                   c.fail(up.error().code, up.error().message);
                   return 0;
               }
               auto blit = host.display->blit(slot, draw_x, y, kTextSlot, 0, 0, img->width, img->height, 0, 0x100);
               host.display->free_slot(kTextSlot);
               if (!blit) c.fail(blit.error().code, blit.error().message);
               else c.push(static_cast<Word>(img->lines));
               return 0;
           };
    r.add({0x919D, "draw text with font id", 15, 1, [draw_text](Ctx& c) { return draw_text(c, true); }});
    r.add({0x929C, "draw text", 21, 1, [draw_text](Ctx& c) { return draw_text(c, false); }});
}

} // namespace obgi
