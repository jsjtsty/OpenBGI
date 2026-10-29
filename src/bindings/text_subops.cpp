#include "bindings/text_subops.hpp"

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
    r.add({0x929C, "draw text", 21, 1, [&host](Ctx& c) {
               // pops are the reverse of the script's push order (analysis/文字绘制.md)
               c.pop();                                   // 21 reserved
               Word eff_alpha = c.pop(), eff_color = c.pop(), eff_b = c.pop(), eff_a = c.pop(), eff_mode = c.pop();
               (void)eff_alpha, (void)eff_color, (void)eff_b, (void)eff_a;
               auto line_gap = static_cast<std::int32_t>(c.pop());
               Word kinsoku = c.pop(), vertical = c.pop();
               (void)kinsoku;
               c.pop();
               c.pop(); // font parameters 2 and 3
               Word size = c.pop();
               std::string font_name = c.cstring(c.pop());
               c.pop();
               c.pop();
               c.pop(); // ruby: parameter, dictionary address, enable flag
               Word color = c.pop();
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
               (void)eff_mode; // outline/shadow effects are not rendered yet

               auto font = host.fonts->open(font_name, static_cast<int>(size));
               if (!font) {
                   c.fail(font.error().code, font.error().message);
                   return 0;
               }
               TextStyle style;
               style.color = color;
               style.line_gap = line_gap;
               if (vertical) {
                   // Simplified vertical writing (not checked against the original): characters run top to bottom from (x, y),
                   // a new line starts a column to the left, and a column also ends at the bottom of the image.
                   const std::int32_t cell = static_cast<std::int32_t>(size), col_w = cell + line_gap;
                   std::int32_t col = 0, row = 0;
                   const bool utf8 = host.code_page && *host.code_page == 65001;
                   for (char32_t ch : utf8 ? utf8_to_utf32(text) : cp932_to_utf32(text)) {
                       if (ch == char32_t{10} || row * cell + cell > static_cast<std::int32_t>(info->height) - y) {
                           ++col, row = 0;
                           if (ch == char32_t{10}) continue;
                       }
                       if (ch == char32_t{13}) continue;
                       auto glyph = render_text(**font, std::u32string(1, ch), style, static_cast<std::uint32_t>(cell) * 2);
                       if (!glyph) continue;
                       if (glyph->width != 0 && glyph->height != 0 &&
                           host.display->upload_slot(kTextSlot, glyph->width, glyph->height, SlotFormat::Bgra, glyph->pixels)) {
                           (void)host.display->blit(slot, x - col * col_w, y + row * cell, kTextSlot, 0, 0, glyph->width, glyph->height, 0, 0x100);
                           host.display->free_slot(kTextSlot);
                       }
                       ++row;
                   }
                   c.push(static_cast<Word>(col + 1));
                   return 0;
               }
               const std::int64_t room = std::int64_t{info->width} - x;
               auto img = render_text(**font, host.code_page && *host.code_page == 65001 ? utf8_to_utf32(text) : cp932_to_utf32(text), style, room > 0 ? static_cast<std::uint32_t>(room) : 1);
               if (!img) {
                   c.fail(img.error().code, img.error().message);
                   return 0;
               }
               if (auto up = host.display->upload_slot(kTextSlot, img->width, img->height, SlotFormat::Bgra, img->pixels); !up) {
                   c.fail(up.error().code, up.error().message);
                   return 0;
               }
               auto blit = host.display->blit(slot, x, y, kTextSlot, 0, 0, img->width, img->height, 0, 0x100);
               host.display->free_slot(kTextSlot);
               if (!blit) c.fail(blit.error().code, blit.error().message);
               else c.push(static_cast<Word>(img->lines));
               return 0;
           }});
}

} // namespace obgi
