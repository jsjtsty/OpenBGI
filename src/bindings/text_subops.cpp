#include "bindings/text_subops.hpp"

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
               auto img = render_text(**font, host.code_page && *host.code_page == 65001 ? utf8_to_utf32(text) : cp932_to_utf32(text), style, room > 0 ? static_cast<std::uint32_t>(room) : 1);
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
    r.add({0x929C, "draw text", 21, 1, [&host](Ctx& c) {
               // pops are the reverse of the script's push order (analysis/文字绘制.md)
               c.pop();                                   // 21 reserved
               Word eff_alpha = c.pop(), eff_color = c.pop(), eff_b = c.pop(), eff_a = c.pop(), eff_mode = c.pop();
               (void)eff_alpha, (void)eff_color, (void)eff_b, (void)eff_a;
               auto line_gap = static_cast<std::int32_t>(c.pop());
               Word hang = c.pop(), prop = c.pop(); // hanging punctuation, proportional glyph widths (not modelled)
               (void)hang, (void)prop;
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
               if (std::getenv("OBGI_DBGTEXT"))
                   std::fprintf(stderr, "92:9C slot %u at %d,%d size %u prop %u str=%s\n", slot, x, y, size, prop, text.c_str());
               (void)eff_mode; // outline/shadow effects are not rendered yet

               auto font = host.fonts->open(font_name, static_cast<int>(size));
               if (!font) {
                   c.fail(font.error().code, font.error().message);
                   return 0;
               }
               TextStyle style;
               style.color = color;
               style.line_gap = line_gap;
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
