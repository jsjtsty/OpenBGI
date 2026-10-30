#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <cstdlib>
#include <cstdio>
#include <functional>
#include <optional>
#include "bindings/graphics_subops.hpp"
#include "render/background_fx.hpp"

#include <string>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

constexpr Word kMaxSlot = 0x3fff;
constexpr Word kScreenSlot = 3794; // 0xED2, read by scrgrp2 before any script creates it
constexpr Word kErrNoSlot = 0x80000003; // what the engine's slot table reports for an unknown slot

bool slot_ok(Ctx& c, Word id) {
    if (id > kMaxSlot) c.fail(Errc::Range, "image slot " + std::to_string(id) + " out of range");
    return c.ok();
}

void check(Ctx& c, const Result<void>& r) {
    if (!r) c.fail(r.error().code, r.error().message);
}

// A script object that does not exist is a script error in the original (the handlers throw).
Sprite* object(Ctx& c, Display& d, Word id) {
    Sprite* s = d.find_sprite(id);
    if (!s && (id == 0 || (id >> 28) == 9 || (id >> 28) == 0xA)) { // filter objects (90:60) are display objects too: kept invisible, effects not drawn yet
        if (id != 0) {
            s = &d.sprite(id);
            s->visible = false;
            return s;
        } // id 0 is the root (screen) object of the original; it always exists and draws nothing itself
        s = &d.sprite(0);
        s->visible = false;
    }
    if (!s) c.fail(Errc::Range, "no display object " + std::to_string(id));
    return s;
}

} // namespace

void register_graphics_subops(vm::SubopRegistry& r, GraphicsHost& host) {
    Display& d = *host.display;

    // The base size of every slot (bmpmgr.c): 0 x 0 until something sets it, -1 x -1 once its bitmap was freed, and the base size
    // in the header of a loaded picture (90:10); 92:12 sets it, 92:16 reads it. Scripts place pictures by it (90:5C's origin).
    auto base_sizes = std::make_shared<std::map<Word, std::pair<std::int32_t, std::int32_t>>>();
    // ---- image slots ----
    add(r, 0x9010, "load image", 3, 0, [&host, &d, base_sizes](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word slot = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:10 slot %u <- %s/%s\n", slot, prefix.c_str(), name.c_str());
        d.vecmaps.erase(slot);
        auto img = host.images->load(prefix, name);
        if (!img) {
            c.fail(img.error().code, "\"" + prefix + "/" + name + "\": " + img.error().message);
            return 0;
        }
        check(c, d.upload_slot(slot, img->width, img->height, img->format, img->pixels));
        if (img->has_base) (*base_sizes)[slot] = {static_cast<std::int32_t>(img->base_width), static_cast<std::int32_t>(img->base_height)};
        return 0; // the original may go asynchronous (rc 2); loading here is always synchronous
    });
    // modes 4-6 (vector, height, vector + distance) are the vector-map bitmaps, kept by Display::vecmaps under the slot number
    add(r, 0x9011, "create image", 4, 0, [&d](Ctx& c) {
        Word format = c.pop(), h = c.pop(), w = c.pop(), slot = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        if (format > 6) {
            c.fail(Errc::Range, "bad image format " + std::to_string(format));
            return 0;
        }
        ++d.vecmap_epoch;
        if (format > 3) {
            (void)d.free_slot(slot);
            d.vecmaps[slot] = vecmap::Map(static_cast<vecmap::Mode>(format), w, h);
            return 0;
        }
        d.vecmaps.erase(slot);
        check(c, d.create_slot(slot, w, h, static_cast<SlotFormat>(format)));
        return 0;
    });
    // The background (object 0) of type 4, the only kind the title and scenes use here (analysis/背景.md): a lower picture at
    // screen position (-x2, -y2), drawn opaque, and an upper picture at (-x1, -y1) over it with transparency `level`
    // (0 = fully shown). `gray` (a wipe map) is not followed: the two are cross-faded plainly. Numbers 0x7000 / 0x7001 / 0x7FFF / -1
    // for the lower picture mean black / white / none; the screen is black anyway. 90:4C (visible, mode) shows or hides it.
    struct Background { bool visible = true; Word type = 1; }; // type: 1..12, the number of the last background op (90:40 = 1 ... 90:4A = 11)
    auto bg = std::make_shared<Background>();
    constexpr Word kBgLower = 0x7C000000, kBgUpper = 0x7C000001;
    auto bg_picture = [&d](Word id, std::int32_t bmp, std::int32_t x, std::int32_t y, Word alpha, bool visible) {
        Sprite& s = d.sprite(id);
        const bool real = bmp >= 0 && bmp < 0x7000 && d.slot(static_cast<Word>(bmp)) != nullptr;
        s.slot = real ? static_cast<Word>(bmp) : 0;
        s.x = -x, s.y = -y;
        s.priority = 0, s.blend = 1, s.alpha = alpha;
        s.background = true;
        s.flip_position = id == kBgUpper;
        s.through_gray = false, s.wipe_gray = 0xFFFFFFFFu, s.progress = 0x100;
        s.visible = real && visible;
    };
    add(r, 0x9043, "background two pictures", 9, 0, [bg, bg_picture, &d](Ctx& c) {
        const Word level = c.pop();
        const Word wipe_param = c.pop();
        const auto wipe_map = static_cast<std::int32_t>(c.pop());
        const auto bmp2 = static_cast<std::int32_t>(c.pop());
        const auto y2 = static_cast<std::int32_t>(c.pop()), x2 = static_cast<std::int32_t>(c.pop());
        const auto bmp1 = static_cast<std::int32_t>(c.pop());
        const auto y1 = static_cast<std::int32_t>(c.pop()), x1 = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        if (std::getenv("OBGI_DBGBG")) {
            const SlotInfo* si = bmp1 >= 0 ? d.slot(static_cast<Word>(bmp1)) : nullptr;
            std::fprintf(stderr, "90:43 upper %d (%ux%u) at %d,%d lower %d at %d,%d level %u wipe map %d param %u\n", bmp1, si ? si->width : 0, si ? si->height : 0, x1, y1,
                         bmp2, x2, y2, level, wipe_map, wipe_param);
        }
        bg->type = 4;
        d.bg_fx = {};
        bg_picture(kBgLower, bmp2, x2, y2, 0, bg->visible);
        bg_picture(kBgUpper, bmp1, x1, y1, level, bg->visible);
        // with a gray map the upper picture is revealed through it (level 0 = all shown) instead of faded by `level`
        const SlotInfo* gi = wipe_map >= 0 && wipe_map < 0x4000 ? d.slot(static_cast<Word>(wipe_map)) : nullptr;
        if (Sprite* up = d.find_sprite(kBgUpper); up && gi && gi->format == SlotFormat::Gray8) {
            up->through_gray = true, up->wipe_gray = static_cast<Word>(wipe_map), up->wipe_mode = wipe_param;
            up->progress = level, up->alpha = 0;
        }
        return 0;
    });
    // The background types rendered on the CPU (Display::render_background_fx): the object becomes the type, with the level 0 and
    // the position (0, 0) a new background object has (gfx/mgr/background.c: Gfx_SetBgType); the upper sprite then only drives them.
    auto revisions = std::make_shared<std::uint64_t>(0);
    auto fx_select = [bg, revisions, &d](int type, Word level) {
        bg->type = static_cast<Word>(type);
        d.bg_fx = {};
        d.bg_fx.type = type, d.bg_fx.revision = ++*revisions;
        d.bg_visible = bg->visible;
        Sprite& up = d.sprite(kBgUpper);
        up.x = up.y = 0, up.alpha = level, up.fade = 0, up.opacity = 0x100, up.flip_position = true;
    };
    // a picture a background type can use: in a slot, and screen sized unless `any_size`
    auto bg_usable = [&d](std::int32_t bmp, bool any_size) {
        if (bmp < 0 || bmp >= 0x4000) return false;
        const SlotInfo* si = d.slot(static_cast<Word>(bmp));
        return si && (any_size || (si->width == d.screen_width() && si->height == d.screen_height()));
    };
    const auto bg_colour = [](std::int32_t bmp) { return bmp == 0x7000 || bmp == 0x7001; }; // BG_BLACK, BG_WHITE
    // 90:41 (bmp, second, level): type 2, the picture cross-faded with a second picture (or black / white) by the level
    add(r, 0x9041, "background crossfade", 3, 0, [=, &d](Ctx& c) {
        const Word level = c.pop();
        const auto second = static_cast<std::int32_t>(c.pop()), bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        fx_select(2, level);
        if (!bg_usable(bmp, false) || !(bg_colour(second) || bg_usable(second, false))) {
            c.fail(Errc::Range, "background bitmap " + std::to_string(bmp) + " or " + std::to_string(second) + " is unusable");
            return 0;
        }
        d.bg_fx.bmp[0] = bmp, d.bg_fx.second = second;
        return 0;
    });
    // 90:42 (b0, b1, b2, b3, x, y): type 3, a window at (x, y) of the 2 x 2 tiling of four screen-sized pictures
    add(r, 0x9042, "background tiled view", 6, 0, [=, &d](Ctx& c) {
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        std::int32_t b[4];
        for (int i = 3; i >= 0; --i) b[i] = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        fx_select(3, 0);
        if (x < 0 || y < 0 || x > static_cast<std::int32_t>(d.screen_width()) || y > static_cast<std::int32_t>(d.screen_height())) {
            c.fail(Errc::Range, "background origin " + std::to_string(x) + "," + std::to_string(y) + " is out of range");
            return 0;
        }
        d.sprite(kBgUpper).script_move_to(x, y);
        d.bg_fx.origin_x = x, d.bg_fx.origin_y = y;
        for (int i = 0; i < 4; ++i) {
            if (!bg_usable(b[i], false)) { c.fail(Errc::Range, "background bitmap " + std::to_string(b[i]) + " is unusable"); return 0; }
            d.bg_fx.bmp[i] = b[i];
        }
        return 0;
    });
    // 90:44 (n, list, level): type 5, a flip book of 2..32 screen-sized pictures; the level is the frame shown
    add(r, 0x9044, "background flip book", 3, 0, [=, &d](Ctx& c) {
        const auto level = static_cast<std::int32_t>(c.pop());
        const Word list = c.pop(), n = c.pop();
        if (!c.ok()) return 0;
        fx_select(5, static_cast<Word>(level));
        if (n < 2 || n > 32) { c.fail(Errc::Range, "invalid background bitmap count " + std::to_string(n)); return 0; }
        for (Word i = 0; i < n; ++i) {
            const auto bmp = static_cast<std::int32_t>(c.read(list + 4 * i, 4, false));
            if (!c.ok()) return 0;
            if (!bg_usable(bmp, false)) { c.fail(Errc::Range, "background bitmap " + std::to_string(bmp) + " is unusable"); return 0; }
            d.bg_fx.frames.push_back(bmp);
        }
        return 0;
    });
    // 90:46 (bmp, type, level): type 7, the picture through a horizontal box blur (edge 0 pads with black, 1 repeats it)
    add(r, 0x9046, "background blur", 3, 0, [=, &d](Ctx& c) {
        const Word level = c.pop(), edge = c.pop();
        const auto bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        fx_select(7, 0);
        if (!bg_usable(bmp, false)) { c.fail(Errc::Range, "background bitmap " + std::to_string(bmp) + " is unusable"); return 0; }
        if (edge > 1) { c.fail(Errc::Range, "invalid gradient type " + std::to_string(edge)); return 0; }
        d.bg_fx.bmp[0] = bmp, d.bg_fx.edge = static_cast<std::int32_t>(edge);
        d.sprite(kBgUpper).alpha = level;
        return 0;
    });
    // 90:48 (bmp, x, y, w, h): type 9, a w x h view of a picture of any size at (x, y) stretched over the screen
    add(r, 0x9048, "background stretched view", 5, 0, [=, &d](Ctx& c) {
        const auto h = static_cast<std::int32_t>(c.pop()), w = static_cast<std::int32_t>(c.pop());
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const auto bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        fx_select(9, 0);
        if (!bg_usable(bmp, true)) { c.fail(Errc::Range, "background bitmap " + std::to_string(bmp) + " is unusable"); return 0; }
        if (static_cast<Word>(w) < 2 || static_cast<Word>(h) < 2) { c.fail(Errc::Range, "invalid view size " + std::to_string(w) + "x" + std::to_string(h)); return 0; }
        d.bg_fx.bmp[0] = bmp, d.bg_fx.view_w = w, d.bg_fx.view_h = h;
        d.sprite(kBgUpper).script_move_to(static_cast<std::int32_t>(static_cast<Word>(x) << 16), static_cast<std::int32_t>(static_cast<Word>(y) << 16)); // 16.16
        return 0;
    });
    // 90:49 (bmp, scale, option): type 10, a picture of any size zoomed by `scale` (16.16, not 0) and rotated by `option` (16.16 degrees)
    add(r, 0x9049, "background zoom", 3, 0, [=, &d](Ctx& c) {
        const auto option = static_cast<std::int32_t>(c.pop()), scale = static_cast<std::int32_t>(c.pop()), bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        fx_select(10, 0);
        if (!bg_usable(bmp, true)) { c.fail(Errc::Range, "background bitmap " + std::to_string(bmp) + " is unusable"); return 0; }
        if (scale == 0) { c.fail(Errc::Range, "invalid scale 0"); return 0; }
        d.bg_fx.bmp[0] = bmp, d.bg_fx.scale = scale, d.bg_fx.angle = option;
        return 0;
    });
    // 90:40 (bmp): the plain background picture
    add(r, 0x9040, "background picture", 1, 0, [bg, bg_picture, &d](Ctx& c) {
        const auto bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        d.bg_fx = {};
        bg->type = 1;
        bg_picture(kBgLower, bmp, 0, 0, 0, bg->visible);
        bg_picture(kBgUpper, -1, 0, 0, 0, false);
        return 0;
    });
    // 90:4A (front, back, style, level, link): type 11, the mosaic transition from front to back (a picture, black or white)
    add(r, 0x904A, "background flip", 5, 0, [=, &d](Ctx& c) {
        const Word link = c.pop(), level = c.pop(), style = c.pop();
        const auto back = static_cast<std::int32_t>(c.pop()), front = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        fx_select(11, 0);
        if (!bg_usable(front, false)) { c.fail(Errc::Range, "background front bitmap " + std::to_string(front) + " is unusable"); return 0; }
        if (!bg_colour(back) && !bg_usable(back, false)) { c.fail(Errc::Range, "background back bitmap " + std::to_string(back) + " is unusable"); return 0; }
        if (style != 0) { c.fail(Errc::Range, "invalid background style " + std::to_string(style)); return 0; }
        if (link > 1) { c.fail(Errc::Range, "invalid background link state " + std::to_string(link)); return 0; }
        d.bg_fx.bmp[0] = front, d.bg_fx.second = back, d.bg_fx.style = 0, d.bg_fx.link = static_cast<std::int32_t>(link);
        d.sprite(kBgUpper).alpha = level;
        return 0;
    });
    add(r, 0x904D, "background type", 0, 1, [bg](Ctx& c) { c.push(bg->type); return 0; });
    // 90:4C (visible, mode): shows or hides the background; mode 0 paints black, anything else the type's picture
    add(r, 0x904C, "background show", 2, 0, [bg, &d](Ctx& c) {
        const auto mode = static_cast<std::int32_t>(c.pop());
        bg->visible = c.pop() != 0;
        if (!c.ok()) return 0;
        d.bg_visible = bg->visible, d.bg_mode = mode;
        for (Word id : {kBgLower, kBgUpper})
            if (Sprite* s = d.find_sprite(id)) s->visible = bg->visible && mode != 0 && s->slot != 0 && s->slot != 0xFFFFFFFFu;
        return 0;
    });
    // 92:12 (slot, w, h): sets the base size of a slot (the design size of its image); 1 for a valid slot number.
    // 92:16 (out, slot): reads it into two words at out (0, 0 until something sets it, -1, -1 after the bitmap was freed).
    add(r, 0x9212, "set image base size", 3, 1, [base_sizes](Ctx& c) {
        const auto h = static_cast<std::int32_t>(c.pop()), w = static_cast<std::int32_t>(c.pop());
        const Word slot = c.pop();
        if (!c.ok()) return 0;
        if (slot <= kMaxSlot) (*base_sizes)[slot] = {w, h};
        c.push(slot <= kMaxSlot ? 1 : 0);
        return 0;
    });
    add(r, 0x9216, "image base size", 2, 1, [base_sizes](Ctx& c) {
        const Word slot = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        if (slot > kMaxSlot) { c.push(0); return 0; }
        std::int32_t w = 0, h = 0;
        if (auto it = base_sizes->find(slot); it != base_sizes->end()) w = it->second.first, h = it->second.second;
        c.write(out, static_cast<Word>(w), 4);
        c.write(out + 4, static_cast<Word>(h), 4);
        c.push(1);
        return 0;
    });
    add(r, 0x9012, "free image", 1, 1, [&d, base_sizes](Ctx& c) {
        Word slot = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:12 free slot %u\n", slot);
        ++d.vecmap_epoch;
        const bool freed = static_cast<bool>(d.free_slot(slot)) || d.vecmaps.erase(slot) != 0;
        if (freed) (*base_sizes)[slot] = {-1, -1}; // bmpmgr.c: BmpMgr_FreeSlot marks the base size -1
        c.push(freed ? 1 : 0); // verified in the real game: 1 freed, 0 no such slot
        return 0;
    });
    add(r, 0x9013, "fill image", 2, 0, [&d](Ctx& c) {
        Word color = c.pop(), slot = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        check(c, d.fill_slot(slot, color));
        return 0;
    });
    // (slot, width, height, format, address): creates the slot and fills it from packed 24-bit BGR rows in script
    // memory. The original only converts format 1; for the others the slot is left empty.
    add(r, 0x9014, "image from memory", 5, 0, [&d](Ctx& c) {
        const Word addr = c.pop(), format = c.pop(), h = c.pop(), w = c.pop(), slot = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        if (format > 3 || w == 0 || h == 0 || w > 0x4000 || h > 0x4000) return 0;
        std::vector<std::uint32_t> px(std::size_t(w) * h, 0);
        if (format == 1) {
            std::vector<std::uint8_t> raw;
            c.read_bytes(addr, px.size() * 3, raw);
            if (!c.ok()) return 0;
            for (std::size_t i = 0; i < px.size(); ++i)
                px[i] = raw[i * 3] | (raw[i * 3 + 1] << 8) | (raw[i * 3 + 2] << 16);
        }
        check(c, d.upload_slot(slot, w, h, static_cast<SlotFormat>(format), px));
        return 0;
    });
    // (destination, size address, capacity, slot): writes the slot's pixels (2/3/4/1 bytes each by format, rows without
    // padding) and their total size; nothing happens when the capacity is too small or the slot does not exist
    add(r, 0x9015, "image to memory", 4, 0, [&d](Ctx& c) {
        const Word slot = c.pop(), capacity = c.pop(), size_addr = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        const SlotInfo* info = d.slot(slot);
        if (!info) return 0;
        static constexpr unsigned kBytes[4] = {2, 3, 4, 1};
        const unsigned bpp = kBytes[static_cast<unsigned>(info->format) & 3];
        const std::uint64_t total = std::uint64_t(info->width) * info->height * bpp;
        if (capacity < total) return 0;
        auto px = d.read_slot(slot);
        if (!px) {
            check(c, Result<void>(std::unexpected(px.error())));
            return 0;
        }
        std::vector<std::uint8_t> out(static_cast<std::size_t>(total));
        for (std::size_t i = 0; i < px->size(); ++i)
            for (unsigned b = 0; b < bpp; ++b) out[i * bpp + b] = static_cast<std::uint8_t>((*px)[i] >> (8 * b));
        c.write_bytes(dst, out.data(), out.size());
        if (size_addr != 0) c.write(size_addr, static_cast<Word>(total), 4);
        return 0;
    });
    // (address, slot): writes the slot description (pitch, width, height, format, bytes per pixel; the first field,
    // the pixel pointer, is always 0 here) and pushes 1, or only clears the pointer and pushes 0 for an unknown slot
    add(r, 0x9016, "image info", 2, 1, [&d](Ctx& c) {
        const Word slot = c.pop(), addr = c.pop();
        if (!c.ok()) return 0;
        const SlotInfo* info = d.slot(slot);
        c.write(addr, 0, 4);
        if (info) {
            static constexpr unsigned kBytes[4] = {2, 3, 4, 1};
            const unsigned bpp = kBytes[static_cast<unsigned>(info->format) & 3];
            c.write(addr + 4, info->width * bpp, 4);
            c.write(addr + 8, info->width, 4);
            c.write(addr + 12, info->height, 4);
            c.write(addr + 16, static_cast<Word>(info->format), 4);
            c.write(addr + 20, bpp, 4);
        }
        c.push(info ? 1 : 0);
        return 0;
    });
    // (slot, format): 2 -> 1 drops the alpha channel's meaning; the same format is a no-op. Pushes 0, 1 (no such
    // slot) or 2 (other conversion)
    add(r, 0x9017, "convert image format", 2, 1, [&d](Ctx& c) {
        const Word format = c.pop(), slot = c.pop();
        if (!c.ok()) return 0;
        const SlotInfo* info = d.slot(slot);
        if (!info) {
            c.push(1);
            return 0;
        }
        if (static_cast<Word>(info->format) != format) {
            if (info->format != SlotFormat::Bgra || format != 1) {
                c.push(2);
                return 0;
            }
            auto px = d.read_slot(slot);
            if (!px) {
                check(c, Result<void>(std::unexpected(px.error())));
                return 0;
            }
            check(c, d.upload_slot(slot, info->width, info->height, SlotFormat::Bgrx, *px));
        }
        c.push(0);
        return 0;
    });
    add(r, 0x9018, "blit image", 6, 0, [&d](Ctx& c) {
        Word alpha = c.pop(), mode = c.pop(), src = c.pop();
        auto dy = static_cast<std::int32_t>(c.pop());
        auto dx = static_cast<std::int32_t>(c.pop());
        Word dst = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:18 blit %u <- %u at %d,%d mode %u alpha %u (src %s, dst %s)\n", dst, src, dx, dy, mode, alpha, d.slot(src) ? "ok" : "missing", d.slot(dst) ? "ok" : "missing");
        const SlotInfo* s = d.slot(src);
        if (src == kScreenSlot) { // the screen image slot (read without ever being created): the composed screen, a guess
            check(c, d.compose());
            auto px = d.read_screen();
            if (c.ok() && px) check(c, d.upload_slot(src, d.screen_width(), d.screen_height(), SlotFormat::Bgrx, *px));
            s = d.slot(src);
        } else if (!s && src > kScreenSlot - 3 && src < kScreenSlot) { // 3792.. neighbours: blank until a script fills them
            if (auto made = d.create_slot(src, d.screen_width(), d.screen_height(), SlotFormat::Bgrx); !made) check(c, made);
            s = d.slot(src);
        }
        if (!s) {
            c.fail(Errc::Range, "no image slot " + std::to_string(src));
            return 0;
        }
        check(c, d.blit(dst, dx, dy, src, 0, 0, s->width, s->height, mode, alpha));
        return 0;
    });
    // 90:1E (dst, dx, dy, src, sx, sy, w, h): plain copy of the part of src at (sx, sy) to (dx, dy); an empty size is not an error
    add(r, 0x901E, "copy rectangle", 8, 0, [&d](Ctx& c) {
        const Word h = c.pop(), w = c.pop();
        const auto sy = static_cast<std::int32_t>(c.pop()), sx = static_cast<std::int32_t>(c.pop());
        const Word src = c.pop();
        const auto dy = static_cast<std::int32_t>(c.pop()), dx = static_cast<std::int32_t>(c.pop());
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        if (!d.slot(dst)) { c.fail(Errc::Range, "no image slot " + std::to_string(dst)); return 0; }
        if (!d.slot(src)) { c.fail(Errc::Range, "no image slot " + std::to_string(src)); return 0; }
        if (w == 0 || h == 0) return 0;
        check(c, d.blit(dst, dx, dy, src, sx, sy, w, h, 0x80, 0));
        return 0;
    });
    // 90:04 (slot): the slot becomes a screen-sized copy of the composed screen
    add(r, 0x9004, "snapshot", 1, 0, [&d](Ctx& c) {
        const Word slot = c.pop();
        if (!c.ok()) return 0;
        check(c, d.compose());
        auto px = d.read_screen();
        if (!c.ok() || !px) return 0;
        check(c, d.upload_slot(slot, d.screen_width(), d.screen_height(), SlotFormat::Bgrx, *px));
        return 0;
    });
    // 92:17 (out, slot, x, y): copies the pixel at x, y of the slot to the address (a packed pixel in the slot's format) and
    // pushes 0; 1 no such slot, 3 the point lies outside.
    add(r, 0x9217, "read pixel", 4, 1, [&d](Ctx& c) {
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word slot = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        const SlotInfo* info = d.slot(slot);
        if (!info) {
            c.push(1);
            return 0;
        }
        if (x < 0 || y < 0 || x >= static_cast<std::int32_t>(info->width) || y >= static_cast<std::int32_t>(info->height)) {
            c.push(3);
            return 0;
        }
        auto px = d.read_slot(slot);
        if (!px) {
            c.fail(px.error().code, px.error().message);
            return 0;
        }
        c.write(out, (*px)[std::size_t(y) * info->width + std::size_t(x)], 4);
        c.push(0);
        return 0;
    });
    // 90:19 (dst, dx, dy, src, mask, mode, alpha): the blit with a third bitmap. Without a bitmap at `mask` the original falls back to
    // the plain blit; with one the mask is not read yet and the plain blit is drawn as well.
    add(r, 0x9019, "blit image with mask", 7, 0, [&d](Ctx& c) {
        const Word alpha = c.pop(), mode = c.pop(), mask = c.pop(), src = c.pop();
        const auto dy = static_cast<std::int32_t>(c.pop()), dx = static_cast<std::int32_t>(c.pop());
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:19 blit %u <- %u mask %u (%s) at %d,%d mode %u alpha %u\n", dst, src, mask, d.slot(mask) ? "ok" : "missing", dx, dy, mode, alpha);
        const SlotInfo* s = d.slot(src);
        if (!d.slot(dst) || !s) {
            c.fail(Errc::Range, "no image slot " + std::to_string(d.slot(dst) ? src : dst));
            return 0;
        }
        check(c, d.blit(dst, dx, dy, src, 0, 0, s->width, s->height, mode, alpha));
        return 0;
    });
    add(r, 0x901F, "crop image", 6, 0, [&d](Ctx& c) {
        Word h = c.pop(), w = c.pop();
        auto y = static_cast<std::int32_t>(c.pop());
        auto x = static_cast<std::int32_t>(c.pop());
        Word src = c.pop(), dst = c.pop();
        if (!c.ok() || !slot_ok(c, dst)) return 0;
        if (src == kScreenSlot) { // the screen image slot is read without ever being created: the composed screen, as in the blit above
            check(c, d.compose());
            auto px = d.read_screen();
            if (c.ok() && px) check(c, d.upload_slot(src, d.screen_width(), d.screen_height(), SlotFormat::Bgrx, *px));
            if (!c.ok()) return 0;
        } else if (!d.slot(src) && src >= kScreenSlot - 4 && src < kScreenSlot) { // neighbours of the screen slot: blank until filled
            check(c, d.create_slot(src, d.screen_width(), d.screen_height(), SlotFormat::Bgrx));
            if (!c.ok()) return 0;
        }
        const SlotInfo* s = d.slot(src);
        if (!s || w == 0 || h == 0) {
            c.fail(Errc::Range, "bad crop source or size (dst " + std::to_string(dst) + " src " + std::to_string(src) + " at " + std::to_string(x) + "," + std::to_string(y) + " size " + std::to_string(w) + "x" + std::to_string(h) + ")");
            return 0;
        }
        if (src == dst) {
            c.fail(Errc::Unsupported, "crop into its own source slot");
            return 0;
        }
        auto created = d.create_slot(dst, w, h, s->format);
        check(c, created);
        if (!created) return 0;
        check(c, d.blit(dst, 0, 0, src, x, y, w, h, 0, 0x100));
        return 0;
    });

    // (destination, source, x scale, y scale (16.16), smooth): a new slot holding the source scaled; smooth 0 picks the nearest
    // pixel, else the four neighbours are blended per channel. The original refuses non-true-colour sources (not modelled).
    add(r, 0x911C, "resample image", 5, 0, [&d](Ctx& c) {
        const Word smooth = c.pop();
        const auto sy = static_cast<std::int32_t>(c.pop()), sx = static_cast<std::int32_t>(c.pop());
        const Word src = c.pop(), dst = c.pop();
        if (!c.ok() || !slot_ok(c, dst)) return 0;
        const SlotInfo* s = d.slot(src);
        if (!s || src == dst || sx <= 0 || sy <= 0) {
            c.fail(Errc::Range, "bad resample source or rate");
            return 0;
        }
        const SlotInfo info = *s;
        const std::uint64_t nw = std::uint64_t{info.width} * static_cast<std::uint32_t>(sx) >> 16, nh = std::uint64_t{info.height} * static_cast<std::uint32_t>(sy) >> 16;
        if (nw == 0 || nh == 0 || nw > 8192 || nh > 8192) {
            c.fail(Errc::Range, "bad resample rate");
            return 0;
        }
        auto px = d.read_slot(src);
        if (!px) {
            c.fail(px.error().code, px.error().message);
            return 0;
        }
        std::vector<std::uint32_t> out(nw * nh);
        const auto at = [&](std::int64_t x, std::int64_t y) {
            x = std::clamp<std::int64_t>(x, 0, info.width - 1), y = std::clamp<std::int64_t>(y, 0, info.height - 1);
            return (*px)[std::size_t(y) * info.width + std::size_t(x)];
        };
        for (std::uint64_t y = 0; y < nh; ++y)
            for (std::uint64_t x = 0; x < nw; ++x) {
                const double fx = (double(x) + 0.5) * info.width / double(nw) - 0.5, fy = (double(y) + 0.5) * info.height / double(nh) - 0.5;
                if (!smooth) {
                    out[y * nw + x] = at(std::int64_t(fx + 0.5), std::int64_t(fy + 0.5));
                    continue;
                }
                const std::int64_t x0 = std::int64_t(std::floor(fx)), y0 = std::int64_t(std::floor(fy));
                const double ax = fx - double(x0), ay = fy - double(y0);
                std::uint32_t v = 0;
                for (int sh = 0; sh < 32; sh += 8) {
                    const auto ch = [&](std::int64_t xx, std::int64_t yy) { return double((at(xx, yy) >> sh) & 0xFF); };
                    const double top = ch(x0, y0) * (1 - ax) + ch(x0 + 1, y0) * ax, bot = ch(x0, y0 + 1) * (1 - ax) + ch(x0 + 1, y0 + 1) * ax;
                    v |= std::uint32_t(std::lround(top * (1 - ay) + bot * ay)) << sh;
                }
                out[y * nw + x] = v;
            }
        check(c, d.upload_slot(dst, std::uint32_t(nw), std::uint32_t(nh), info.format, out));
        return 0;
    });

    // (source, destination): a new slot of the source's size and format with a copy of its image. The original also
    // copies two metadata words (not modelled).
    add(r, 0x911F, "copy image", 2, 0, [&d](Ctx& c) {
        Word src = c.pop(), dst = c.pop();
        if (!c.ok() || !slot_ok(c, dst)) return 0;
        const SlotInfo* s = d.slot(src);
        if (!s || src == dst) {
            c.fail(Errc::Range, "bad copy source or destination");
            return 0;
        }
        const SlotInfo info = *s;
        auto created = d.create_slot(dst, info.width, info.height, info.format);
        check(c, created);
        if (!created) return 0;
        check(c, d.blit(dst, 0, 0, src, 0, 0, info.width, info.height, 0, 0x100));
        return 0;
    });

    // ---- display objects (sprites) ----
    add(r, 0x9056, "set sprite", 7, 0, [&d](Ctx& c) {
        Word priority = c.pop(), alpha = c.pop(), blend = c.pop(), slot = c.pop();
        auto y = static_cast<std::int32_t>(c.pop());
        auto x = static_cast<std::int32_t>(c.pop());
        Word id = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:56 obj %X at %d,%d slot %u blend %u alpha %u prio %u""\n", id, x, y, slot, blend, alpha, priority);
        if (alpha > 0x100 || priority > 0xffff || !blend_mode_supported(blend)) {
            c.fail(Errc::Range, "sprite parameter out of range or unsupported blend mode");
            return 0;
        }
        if (!d.slot(slot)) {
            c.fail(Errc::Range, "no image slot " + std::to_string(slot));
            return 0;
        }
        Sprite& s = d.sprite(id);
        s.slot = slot, s.x = x, s.y = y, s.blend = blend, s.alpha = alpha, s.priority = priority;
        s.projected = false, s.xform = false, s.slot2 = 0xFFFFFFFFu, s.mix_ratio = 0, s.level_route = -1, s.ripple_map = -1;
        return 0;
    });
    // 90:58 (object, x, y, bmp1, bmp2, mix ratio, level, priority, level route): two pictures of the same size cross-faded by the mix ratio
    // (0 = the first, 0x100 = the second); a later level change drives the level (-1, 0), the mix ratio (1) or both (2) as the route says
    // (sprite.c: Sprite_SetBlendEx; the effect is forced to 1). Pictures of different sizes pass silently, as in the reference build.
    add(r, 0x9058, "set blend sprite", 9, 0, [&d](Ctx& c) {
        const auto route = static_cast<std::int32_t>(c.pop());
        const Word priority = c.pop(), level = c.pop(), mix = c.pop();
        const auto bmp2 = static_cast<std::int32_t>(c.pop()), bmp1 = static_cast<std::int32_t>(c.pop());
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word id = c.pop();
        if (!c.ok()) return 0;
        if (mix > 0x100 || level > 0x100 || priority > 0xffff) { c.fail(Errc::Range, "blend sprite parameter out of range"); return 0; }
        const SlotInfo* i1 = bmp1 >= 0 && bmp1 < 0x4000 ? d.slot(static_cast<Word>(bmp1)) : nullptr;
        const SlotInfo* i2 = bmp2 >= 0 && bmp2 < 0x4000 ? d.slot(static_cast<Word>(bmp2)) : nullptr;
        if (!i1 || !i2) { c.fail(Errc::Range, "no image slot " + std::to_string(i1 ? bmp2 : bmp1)); return 0; }
        Sprite& s = d.sprite(id);
        if (i1->width != i2->width || i1->height != i2->height || i1->format != i2->format) return 0;
        s.slot = static_cast<Word>(bmp1), s.slot2 = static_cast<Word>(bmp2), s.mix_ratio = mix, s.level_route = route;
        s.x = x, s.y = y, s.blend = 1, s.alpha = level, s.priority = priority;
        s.projected = false, s.xform = false, s.ripple_map = -1;
        return 0;
    });
    // 90:59 (object, x, y, bmp, ox, oy, angle, sx, sy, smooth, effect, level, priority): one picture rotated by `angle` (16.16 degrees) and scaled by
    // (sx, sy) (16.16) about the point (ox, oy) of the picture, which lands on (x, y); a box under 2 x 2 is an error (sprite.c: Sprite_SetTransformEx)
    add(r, 0x9059, "set transform sprite", 13, 0, [&d](Ctx& c) {
        const Word priority = c.pop(), level = c.pop(), effect = c.pop(), smooth = c.pop();
        const auto sy = static_cast<std::int32_t>(c.pop()), sx = static_cast<std::int32_t>(c.pop()), angle = static_cast<std::int32_t>(c.pop());
        const auto oy = static_cast<std::int32_t>(c.pop()), ox = static_cast<std::int32_t>(c.pop());
        const Word bmp = c.pop();
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word id = c.pop();
        if (!c.ok()) return 0;
        if (level > 0x100 || priority > 0xffff || !blend_mode_supported(effect)) { c.fail(Errc::Range, "transform sprite parameter out of range or unsupported blend mode"); return 0; }
        Sprite& s = d.sprite(id);
        s.x = x, s.y = y, s.blend = effect, s.alpha = level, s.priority = priority; // the common state stays set when the mode is refused
        if (!d.slot(bmp)) { c.fail(Errc::Range, "no image slot " + std::to_string(bmp)); return 0; }
        if (sx <= 0 || sy <= 0) { c.fail(Errc::Range, "invalid scale " + std::to_string(sx) + "," + std::to_string(sy)); return 0; }
        s.slot = bmp, s.slot2 = 0xFFFFFFFFu, s.mix_ratio = 0, s.level_route = -1;
        s.projected = false, s.xform = true, s.ripple_map = -1;
        s.origin_x = ox, s.origin_y = oy, s.angle = angle, s.sx = sx, s.sy = sy, s.smooth = smooth != 0;
        return 0;
    });
    // 90:5A (object, x, y, bmp, gray, wipe mode, progress, effect, level, prio): a sprite whose appearance is revealed through the
    // grey filter bitmap `gray` as `progress` (0..0x100) grows. The wipe itself is not drawn: the picture is shown as a plain sprite.
    add(r, 0x905A, "set wipe sprite", 10, 0, [&d](Ctx& c) {
        const Word priority = c.pop(), level = c.pop(), effect = c.pop(), progress = c.pop(), wipe_mode = c.pop(), gray = c.pop(), slot = c.pop();
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word id = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        if (level > 0x100 || progress > 0x100 || priority > 0xffff || !blend_mode_supported(effect)) {
            c.fail(Errc::Range, "wipe sprite parameter out of range or unsupported blend mode");
            return 0;
        }
        if (!d.slot(slot)) { c.fail(Errc::Range, "no image slot " + std::to_string(slot)); return 0; }
        Sprite& s = d.sprite(id);
        s.slot = slot, s.x = x, s.y = y, s.blend = effect, s.alpha = level, s.priority = priority;
        s.projected = false, s.xform = false, s.slot2 = 0xFFFFFFFFu, s.mix_ratio = 0, s.level_route = -1, s.ripple_map = -1;
        const SlotInfo* gi = d.slot(gray);
        s.wipe_gray = gi && gi->format == SlotFormat::Gray8 ? gray : 0xFFFFFFFFu; // no pattern: shown whole
        s.wipe_mode = wipe_mode;
        d.set_progress(id, progress);
        return 0;
    });
    // 90:35 (object, progress 0..0x100): how far a wipe sprite has been revealed; handed to what hangs on the object
    add(r, 0x9035, "set object progress", 2, 0, [&d](Ctx& c) {
        const Word value = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:35 progress %X = %u\n", id, value);
        if (!object(c, d, id)) return 0;
        if (value > 0x100) c.fail(Errc::Range, "progress out of range");
        else d.set_progress(id, value);
        return 0;
    });
    // 90:5C (17 values, popped top first: priority, alpha, visible, ?, ?, focal distance, 5 unknowns, second slot (-1 = none), image slot,
    // ?, y, x, object): turns an object into a perspective-projected plane showing the image slot. The position is the
    // plane's top-left corner relative to the screen centre in 16.16 fixed point. Only the flat, untransformed case is
    // modelled: the image is drawn at that position at full size (checked against the original's arguments).
    // 90:5D (1.588 on) is 90:5C with the rotations about the X, Y and Z axes between the angle and the projection distance; the
    // reference engine ignores them too (the game's screens call it with all three at 0)
    auto projected = [&d](Ctx& c, const Word (&v)[17]) {
        if (std::getenv("OBGI_DBG5C"))
            std::fprintf(stderr, "[%llu] 90:5C h=%X fx=%d fy=%d fz=%d bmp1=%d bmp2=%d mix=%u route=%d ox=%d oy=%d angle=%d dist=%d projPos=%d smooth=%d effect=%u level=%u prio=%u\n",
                         static_cast<unsigned long long>(c.vm.frame), v[16], static_cast<int>(v[15]), static_cast<int>(v[14]), static_cast<int>(v[13]), static_cast<int>(v[12]), static_cast<int>(v[11]), v[10], static_cast<int>(v[9]),
                         static_cast<int>(v[8]), static_cast<int>(v[7]), static_cast<int>(v[6]), static_cast<int>(v[5]), static_cast<int>(v[4]), static_cast<int>(v[3]), v[2], v[1], v[0]);
        if (std::getenv("OBGI_DBG5C"))
            if (const SlotInfo* si = d.slot(v[12])) std::fprintf(stderr, "90:5C bmp1 %u is %ux%u\n", v[12], si->width, si->height);
        Sprite* s = object(c, d, v[16]);
        if (!s) return 0;
        if (!d.slot(v[12])) {
            c.fail(Errc::Range, "no image slot " + std::to_string(v[12]));
            return 0;
        }
        if (v[1] > 0x100 || v[0] > 0xffff) {
            c.fail(Errc::Range, "projected plane parameter out of range");
            return 0;
        }
        if (v[10] > 0x100) { c.fail(Errc::Range, "mix ratio out of range"); return 0; }
        // sprite.c: Sprite_SetProjectedEx first places the object (depth ordering, the 16.16 position, effect, level through the
        // route of the previous mode, priority), then sets the mode, which may still refuse the pictures
        // the script's position stays relative to the centre (90:29 paths and 90:28 tweens are in that space); the compositor
        // projects it
        s->projected = true, s->xform = false, s->ripple_map = -1;
        s->x = static_cast<std::int32_t>(v[15]) >> 16;
        s->y = static_cast<std::int32_t>(v[14]) >> 16;
        s->depth = static_cast<std::int32_t>(v[13]);
        if (blend_mode_supported(v[2])) s->blend = v[2]; // v[2] is the effect, not a visibility flag
        s->set_level(v[1]);
        s->priority = v[0];
        const auto bmp2 = static_cast<std::int32_t>(v[11]);
        const SlotInfo* i1 = d.slot(v[12]);
        const SlotInfo* i2 = bmp2 != -1 ? d.slot(static_cast<Word>(bmp2)) : nullptr;
        if (bmp2 != -1 && !i2) { c.fail(Errc::Range, "no image slot " + std::to_string(bmp2)); return 0; }
        if (i2 && (i1->width != i2->width || i1->height != i2->height || i1->format != i2->format)) return 0; // passes silently in the reference build
        s->slot = v[12];
        s->dist = static_cast<std::int32_t>(v[5]);
        s->scale_position = v[4] != 0;
        s->origin_x = static_cast<std::int32_t>(v[8]), s->origin_y = static_cast<std::int32_t>(v[7]);
        s->angle = static_cast<std::int32_t>(v[6]), s->smooth = v[3] != 0;
        s->slot2 = i2 ? static_cast<Word>(bmp2) : 0xFFFFFFFFu;
        s->mix_ratio = i2 ? v[10] : 0;
        s->level_route = i2 ? static_cast<std::int32_t>(v[9]) : -1;
        return 0;
    };
    add(r, 0x905C, "projected plane", 17, 0, [projected](Ctx& c) {
        Word v[17];
        for (Word& x : v) x = c.pop();
        if (!c.ok()) return 0;
        return projected(c, v);
    });
    add(r, 0x905D, "projected plane 3D", 20, 0, [projected](Ctx& c) {
        Word w[20], v[17];
        for (Word& x : w) x = c.pop();
        if (!c.ok()) return 0;
        for (int i = 0, j = 0; i < 20; ++i)
            if (i < 6 || i > 8) v[j++] = w[i]; // drop rotZ, rotY, rotX
        return projected(c, v);
    });
    add(r, 0x9057, "set sprite image", 2, 0, [&d](Ctx& c) {
        Word slot = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = object(c, d, id);
        if (!s) return 0;
        if (!d.slot(slot)) {
            c.fail(Errc::Range, "no image slot " + std::to_string(slot));
            return 0;
        }
        s->slot = slot;
        return 0;
    });
    auto visibility = [&d](Ctx& c) {
        Word value = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "visible %X = %u""\n", id, value);
        if (object(c, d, id)) d.set_visible(id, value != 0);
        return 0;
    };
    add(r, 0x9030, "set object visible", 2, 0, visibility);
    add(r, 0x9054, "set sprite visible", 2, 0, visibility);
    add(r, 0x9032, "set object alpha", 2, 0, [&d](Ctx& c) {
        Word alpha = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = object(c, d, id);
        if (!s) return 0;
        if (alpha > 0x100) c.fail(Errc::Range, "alpha out of range");
        else s->set_level(alpha);
        return 0;
    });
    // (object, priority): the draw order of the object (1.69/472 on)
    add(r, 0x903A, "set object priority", 2, 0, [&d](Ctx& c) {
        Word prio = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_DBGPANEL")) std::fprintf(stderr, "90:3A object %X priority %u\n", id, prio);
        if (Sprite* s = object(c, d, id)) s->priority = prio;
        return 0;
    });
    add(r, 0x9033, "move object", 3, 0, [&d](Ctx& c) {
        auto y = static_cast<std::int32_t>(c.pop());
        auto x = static_cast<std::int32_t>(c.pop());
        Word id = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_DBGMOVE")) std::fprintf(stderr, "90:33 object %X to %d,%d\n", id, x, y);
        if (Sprite* s = object(c, d, id)) s->script_move_to(x, y);
        return 0;
    });
    // (object, fade 0..0x100): an extra transparency for the object and what hangs on it
    add(r, 0x9034, "set object fade", 2, 0, [&d](Ctx& c) {
        Word value = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (!object(c, d, id)) return 0;
        if (value > 0x100) c.fail(Errc::Range, "fade out of range");
        else d.set_fade(id, value);
        return 0;
    });
    // (object, opacity 0..0x100): 1.599 on, multiplies in like the fade
    add(r, 0x9039, "set object opacity", 2, 0, [&d](Ctx& c) {
        Word value = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (!object(c, d, id)) return 0;
        if (value > 0x100) c.fail(Errc::Range, "opacity out of range");
        else d.set_opacity(id, value);
        return 0;
    });
    // (object, enabled): a second condition of visibility besides 90:30
    add(r, 0x9031, "set object enabled", 2, 0, [&d](Ctx& c) {
        Word value = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        // dispobj.c: DispObj_SetEnabled passes the flag on to the children when 90:38 0xC0 asked for it
        std::function<void(std::uint32_t)> set = [&](std::uint32_t oid) {
            Sprite* s = d.find_sprite(oid);
            if (!s) return;
            s->enabled = value != 0;
            if (s->enabled_to_children)
                for (auto& [mid, m] : d.sprites())
                    if (m.has_master && m.master == oid) set(mid);
        };
        if (object(c, d, id)) set(id);
        return 0;
    });
    // (object, dx, dy): the draw offsets 90:36 (second) and 90:37, added to the position
    for (const std::uint16_t op : {std::uint16_t(0x9036), std::uint16_t(0x9037)})
        add(r, op, op == 0x9036 ? "set object offset 2" : "set object offset", 3, 0, [&d, op](Ctx& c) {
            const auto dy = static_cast<std::int32_t>(c.pop());
            const auto dx = static_cast<std::int32_t>(c.pop());
            const Word id = c.pop();
            if (!c.ok()) return 0;
            if (Sprite* s = object(c, d, id)) (op == 0x9036 ? s->off2_x : s->off_x) = dx, (op == 0x9036 ? s->off2_y : s->off_y) = dy;
            return 0;
        });
    // 91:38 (out, object, number): reads a parameter of an object into script memory at `out` (dispobj.c: DispObj_GetParam): 0 the
    // position (two words), 1 the blend mode, 2 the level, 3 the priority, 0x20 the fixed position (16.16 x, y, z; z is not kept
    // here). The scripts use it to start a movement from where the object is. Other numbers leave `out` alone.
    add(r, 0x9138, "read object parameter", 3, 0, [&d](Ctx& c) {
        const auto no = static_cast<std::int32_t>(c.pop());
        const Word id = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = object(c, d, id);
        if (!s) return 0;
        switch (no) {
        case 0: c.write(out, static_cast<Word>(s->script_x()), 4), c.write(out + 4, static_cast<Word>(s->script_y()), 4); break;
        case 1: c.write(out, s->blend, 4); break;
        case 2: c.write(out, s->level(), 4); break;
        case 3: c.write(out, s->priority, 4); break;
        case 0x20:
            c.write(out, static_cast<Word>(s->script_x()) << 16, 4), c.write(out + 4, static_cast<Word>(s->script_y()) << 16, 4);
            c.write(out + 8, static_cast<Word>(s->depth), 4);
            break;
        default: break;
        }
        return 0;
    });
    add(r, 0x9038, "set object property", 4, 0, [&d](Ctx& c) {
        Word b = c.pop(), a = c.pop(), prop = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = object(c, d, id);
        if (!s) return 0;
        if (std::getenv("OBGI_SLOTS") && prop > 2) {
            static std::set<std::array<Word, 3>> seen;
            if (seen.insert({prop, a, b}).second) std::fprintf(stderr, "90:38 prop 0x%X a=%u b=%u (object %X)\n", prop, a, b, id);
        }
        switch (prop) {
        case 0: s->script_move_to(static_cast<std::int32_t>(a), static_cast<std::int32_t>(b)); break;
        case 1:
            if (!blend_mode_supported(a)) c.fail(Errc::Unsupported, "blend mode " + std::to_string(a) + " is not implemented");
            else s->blend = a;
            break;
        case 2:
            if (a > 0x100) c.fail(Errc::Range, "alpha out of range");
            else s->set_level(a);
            break;
        // the base class's parameters (dispobj.c: DispObj_SetParam)
        case 0x8100: s->sort_bias = static_cast<std::int32_t>(a); break; // the sort key's bias: equal priorities draw in this order
        case 0x8101:
            if (a > 1) c.fail(Errc::Range, "property 0x8101 takes 0 or 1");
            else s->sort_mode = a;
            break;
        case 0x8000: s->snap = a != 0, s->snap_mode = static_cast<std::int32_t>(b); break;
        case 0x8001: s->no_perspective = a != 0; break;
        case 0xC0: s->enabled_to_children = a != 0; break;
        case 0xC4: s->follow_offset = a != 0; break;
        case 0xC5: s->param_c5 = static_cast<std::int32_t>(a); break;
        case 0xFFFF: { // a = the bits (0: all), b = set or clear them
            const std::uint32_t mask = a ? a : 0xFFFFFFFFu;
            s->flag_bits = b ? (s->flag_bits | mask) : (s->flag_bits & ~mask);
            break;
        }
        case 0x7FFFFFFF:
            if (a >= 16) c.fail(Errc::Range, "script value slot out of range");
            else s->user_values[a] = static_cast<std::int32_t>(b);
            break;
        // not modelled: 0xC1 passes 91:31 on to the children, 0x7FFF0000 redraws every frame (the screen is composed every frame here)
        case 0xC1: case 0x7FFF0000:
            break;
        // parameters of the effect object types (0x41 and 0x80..0x83 apply to types 2, 5 and 6; 0x8F to all): the original only
        // stores them for the effect; the scripts so far reset them to 0 (analysis/显示对象属性.md)
        case 0x41: case 0x81: case 0x82: case 0x83: case 0x8F:
            // the effector's own (effector.c: Effector_SetParam): the angle delta, the scale delta and the easing curve of the zoom mode
            if (s->effector) {
                if (prop == 0x81 && s->fx.mode == 3) s->fx.d_angle = static_cast<std::int32_t>(a);
                else if (prop == 0x82 && s->fx.mode == 3) s->fx.d_sx = static_cast<std::int32_t>(a), s->fx.d_sy = static_cast<std::int32_t>(b);
                else if (prop == 0x8F) s->fx.curve = static_cast<std::int32_t>(a);
            }
            break;
        // the background types' own parameters on handle 0 (zoom.c, stretch.c): 0x80 the zoom and angle change reached at level 0x100,
        // 0x101 a new zoom and angle, 0x102 the target view (x | y << 16, w | h << 16)
        case 0x80:
            if (id == 0 && d.bg_fx.type == 10) {
                if (d.bg_fx.scale + static_cast<std::int32_t>(a) == 0) c.fail(Errc::Range, "invalid zoom change");
                else d.bg_fx.scale_delta = static_cast<std::int32_t>(a), d.bg_fx.angle_delta = static_cast<std::int32_t>(b), ++d.bg_fx.revision;
            } else if (s->effector && s->fx.mode == 3) {
                s->fx.d_cx = static_cast<std::int32_t>(a), s->fx.d_cy = static_cast<std::int32_t>(b);
            }
            break;
        case 0x101:
            if (id == 0 && d.bg_fx.type == 10) {
                if (a == 0) c.fail(Errc::Range, "invalid zoom 0");
                else d.bg_fx.scale = static_cast<std::int32_t>(a), d.bg_fx.angle = static_cast<std::int32_t>(b), d.bg_fx.scale_delta = d.bg_fx.angle_delta = 0, ++d.bg_fx.revision;
            }
            break;
        case 0x100: // type 8 (ripple.c: Bg8_SelectRipple): the ripple's phase selector a and a new level b (above 0x100 keeps the current one)
            if (id == 0 && d.bg_fx.type == 8) {
                bool ok = false;
                if (vecmap::ripple_check(d.ripples, ok, d.bg_fx.ripple_no, static_cast<std::int32_t>(a), d.bg_fx.rings) != 0 || !ok) {
                    c.fail(Errc::Range, "ripple phase " + std::to_string(static_cast<std::int32_t>(a)) + " is not available");
                } else {
                    d.bg_fx.selector = static_cast<std::int32_t>(a);
                    if (b <= 0x100) s->set_level(b);
                    ++d.bg_fx.revision;
                }
            } else if (s->effector) { // effector.c: Effector_SelectRipple, mode 2 only
                if (s->fx.mode == 2) {
                    bool ok = false;
                    if (vecmap::ripple_check(d.ripples, ok, s->fx.ripple_no, static_cast<std::int32_t>(a), s->fx.rings) != 0 || !ok) {
                        c.fail(Errc::Range, "ripple phase " + std::to_string(static_cast<std::int32_t>(a)) + " is not available");
                    } else {
                        s->fx.ripple_sel = static_cast<std::int32_t>(a);
                        s->alpha = b;
                    }
                }
            } else if (s->ripple_map != -1) { // sprite.c: Sprite_SelectRipple, the same for the ripple sprite (90:5B)
                bool ok = false;
                if (vecmap::ripple_check(d.ripples, ok, s->ripple_no, static_cast<std::int32_t>(a), s->rings) != 0 || !ok) {
                    c.fail(Errc::Range, "ripple phase " + std::to_string(static_cast<std::int32_t>(a)) + " is not available");
                } else {
                    s->ripple_sel = static_cast<std::int32_t>(a);
                    if (b <= 0x100) s->set_level(b);
                }
            }
            break;
        case 0x102:
            if (id == 0 && d.bg_fx.type == 9) {
                if ((b & 0xffff) < 2 || (b >> 16) < 2) c.fail(Errc::Range, "invalid view size");
                else {
                    d.bg_fx.target_x = static_cast<std::int16_t>(a & 0xffff), d.bg_fx.target_y = static_cast<std::int16_t>(a >> 16);
                    d.bg_fx.target_w = static_cast<std::int32_t>(b & 0xffff), d.bg_fx.target_h = static_cast<std::int32_t>(b >> 16);
                    ++d.bg_fx.revision;
                }
            }
            break;
        // 0x40 origin (x, y), 0x41 rotation, 0x42 scale (x, y; 16.16, 0 = 1) of the picture types: only the identity is drawn so far
        case 0x40: case 0x42: case 0x43: case 0x60: {
            static std::set<Word> shown;
            const bool identity = prop == 0x42 ? (a == 0x10000 || a == 0) && (b == 0x10000 || b == 0) : prop == 0x40 || prop == 0x43 || prop == 0x60;
            if (!identity && shown.insert(prop).second) std::fprintf(stderr, "90:38: property 0x%X (a=%u b=%u) is stored but not drawn\n", prop, a, b);
            break;
        }
        default: // the reference raises "invalid parameter number" for a number no class supports
            c.fail(Errc::Range, "object parameter 0x" + [&] { char t[16]; std::snprintf(t, sizeof t, "%X", prop); return std::string(t); }() + " is not supported");
            break;
        }
        return 0;
    });

    // ---- bitmap operations on the CPU (gfx/bmpops.c): 90:1B blur, 90:1C stretch, 90:1D rotate, 90:C3 half size ----
    // a slot's pixels if it is a 32-bit picture (Bgrx / Bgra), the only modes the reference's blitters draw
    const auto picture = [&d](std::int32_t id) -> std::optional<bgfx::Image> {
        const SlotInfo* si = id >= 0 && id <= static_cast<std::int32_t>(kMaxSlot) ? d.slot(static_cast<Word>(id)) : nullptr;
        if (!si || (si->format != SlotFormat::Bgra && si->format != SlotFormat::Bgrx)) return std::nullopt;
        auto px = d.read_slot(static_cast<Word>(id));
        if (!px) return std::nullopt;
        return bgfx::Image{si->width, si->height, std::move(*px)};
    };
    const auto exists = [&d](std::int32_t id) { return id >= 0 && id <= static_cast<std::int32_t>(kMaxSlot) && d.slot(static_cast<Word>(id)) != nullptr; };
    // 90:1B (dst, src, type, level): a horizontal box blur of src into dst (same size and mode); type 0 pads the edges with black, 1 repeats them
    add(r, 0x901B, "blur image", 4, 0, [=, &d](Ctx& c) {
        const auto level = static_cast<std::int32_t>(c.pop()), type = static_cast<std::int32_t>(c.pop());
        const auto src = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (!exists(dst)) { c.fail(Errc::Range, "invalid destination bitmap " + std::to_string(dst)); return 0; }
        if (!exists(src)) { c.fail(Errc::Range, "invalid source bitmap " + std::to_string(src)); return 0; }
        if (type != 0 && type != 1) { c.fail(Errc::Range, "invalid gradient type " + std::to_string(type)); return 0; }
        if (static_cast<std::uint32_t>(level) > 0x100) { c.fail(Errc::Range, "invalid effect level " + std::to_string(level)); return 0; }
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *si = d.slot(static_cast<Word>(src));
        if (di->format != si->format || di->width != si->width || di->height != si->height) { c.fail(Errc::Range, "bitmaps " + std::to_string(dst) + " and " + std::to_string(src) + " differ in size or mode"); return 0; }
        const auto in = picture(src);
        if (!in) return 0; // other modes are not drawn
        const bgfx::Image out = bgfx::box_blur(*in, type, level);
        check(c, d.upload_slot(static_cast<Word>(dst), out.w, out.h, si->format, out.px));
        return 0;
    });
    // 90:1C (dst, x, y, w, h, src, sx, sy, sw, sh): the (sx, sy, sw, sh) part of src scaled smoothly into the (x, y, w, h) rectangle of dst,
    // clipped to dst; both sizes at least 2. The scale uses 65540 per unit, and the mapping is centred on the clipped rectangle, as in the original.
    add(r, 0x901C, "stretch image", 10, 0, [=, &d](Ctx& c) {
        const auto sh = static_cast<std::int32_t>(c.pop()), sw = static_cast<std::int32_t>(c.pop()), sy = static_cast<std::int32_t>(c.pop()), sx = static_cast<std::int32_t>(c.pop());
        const auto src = static_cast<std::int32_t>(c.pop());
        const auto h = static_cast<std::int32_t>(c.pop()), w = static_cast<std::int32_t>(c.pop()), y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const auto dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (!exists(dst)) { c.fail(Errc::Range, "invalid destination bitmap " + std::to_string(dst)); return 0; }
        if (!exists(src)) { c.fail(Errc::Range, "invalid source bitmap " + std::to_string(src)); return 0; }
        if (static_cast<std::uint32_t>(w) < 2 || static_cast<std::uint32_t>(h) < 2) { c.fail(Errc::Range, "invalid output range " + std::to_string(w) + "x" + std::to_string(h)); return 0; }
        if (static_cast<std::uint32_t>(sw) < 2 || static_cast<std::uint32_t>(sh) < 2) { c.fail(Errc::Range, "invalid reference range " + std::to_string(sw) + "x" + std::to_string(sh)); return 0; }
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *si = d.slot(static_cast<Word>(src));
        if (di->format != si->format) { c.fail(Errc::Range, "bitmaps " + std::to_string(dst) + " and " + std::to_string(src) + " differ in mode"); return 0; }
        auto from = picture(src);
        auto to = picture(dst);
        if (!from || !to) return 0;
        const std::int64_t l = std::max<std::int64_t>(x, 0), t = std::max<std::int64_t>(y, 0);
        const std::int64_t rr = std::min<std::int64_t>(std::int64_t{x} + w, to->w), bb = std::min<std::int64_t>(std::int64_t{y} + h, to->h);
        if (rr <= l || bb <= t) return 0;
        const auto cw = static_cast<std::uint32_t>(rr - l), ch = static_cast<std::uint32_t>(bb - t);
        const auto fy = static_cast<std::int32_t>(static_cast<double>(h) * 65540.0 / sh), fx = static_cast<std::int32_t>(static_cast<double>(w) * 65540.0 / sw);
        const bgfx::Image part = bgfx::scale2(*from, cw, ch, sx << 16, sy << 16, sw << 16, sh << 16, fx, fy);
        for (std::uint32_t row = 0; row < ch; ++row)
            std::copy_n(&part.px[std::size_t{row} * cw], cw, &to->px[std::size_t(t + row) * to->w + std::size_t(l)]);
        check(c, d.upload_slot(static_cast<Word>(dst), to->w, to->h, di->format, to->px));
        return 0;
    });
    // 90:1D (dst, src, zoom, angle): src zoomed (16.16, above 0) and rotated (16.16 degrees) about its centre into dst, nearest sampling
    add(r, 0x901D, "rotate image", 4, 0, [=, &d](Ctx& c) {
        const auto angle = static_cast<std::int32_t>(c.pop()), zoom = static_cast<std::int32_t>(c.pop());
        const auto src = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (!exists(dst)) { c.fail(Errc::Range, "invalid destination bitmap " + std::to_string(dst)); return 0; }
        if (!exists(src)) { c.fail(Errc::Range, "invalid source bitmap " + std::to_string(src)); return 0; }
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *si = d.slot(static_cast<Word>(src));
        if (di->format != si->format) { c.fail(Errc::Range, "bitmaps " + std::to_string(dst) + " and " + std::to_string(src) + " are incompatible"); return 0; }
        if (zoom <= 0) { c.fail(Errc::Range, "invalid scale " + std::to_string(zoom)); return 0; }
        const auto from = picture(src);
        if (!from) return 0;
        const bgfx::Image out = bgfx::rotate_centred(*from, di->width, di->height, zoom, angle);
        check(c, d.upload_slot(static_cast<Word>(dst), out.w, out.h, di->format, out.px));
        return 0;
    });
    // 90:C3 (dst, src): dst made src at half its size, 2 x 2 pixels averaged with rounding up (the save thumbnails)
    add(r, 0x90C3, "half size image", 2, 0, [=, &d](Ctx& c) {
        const auto src = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (!slot_ok(c, static_cast<Word>(dst)) || !slot_ok(c, static_cast<Word>(src))) return 0;
        if (!exists(src)) { c.fail(Errc::Range, "invalid source bitmap " + std::to_string(src)); return 0; }
        const SlotInfo si = *d.slot(static_cast<Word>(src));
        if (si.width < 2 || si.height < 2) { c.fail(Errc::Range, "source bitmap " + std::to_string(src) + " is too small"); return 0; }
        const auto from = picture(src);
        if (!from) return 0;
        const std::uint32_t ow = (si.width + 1) >> 1, oh = (si.height + 1) >> 1;
        std::vector<std::uint32_t> out(std::size_t{ow} * oh, 0);
        const auto avg = [](unsigned a, unsigned b) { return (a + b + 1) >> 1; };
        const auto at = [&](std::uint32_t x, std::uint32_t y) { return from->px[std::size_t{std::min(y, si.height - 1)} * si.width + std::min(x, si.width - 1)]; };
        for (std::uint32_t y = 0; y < oh; ++y)
            for (std::uint32_t x = 0; x < ow; ++x) {
                const std::uint32_t p[4] = {at(2 * x, 2 * y), at(2 * x + 1, 2 * y), at(2 * x, 2 * y + 1), at(2 * x + 1, 2 * y + 1)};
                std::uint32_t px = 0;
                for (int sh = 0; sh < 32; sh += 8)
                    px |= avg(avg((p[1] >> sh) & 0xff, (p[3] >> sh) & 0xff), avg((p[0] >> sh) & 0xff, (p[2] >> sh) & 0xff)) << sh;
                out[std::size_t{y} * ow + x] = px;
            }
        check(c, d.upload_slot(static_cast<Word>(dst), ow, oh, si.format, out));
        return 0;
    });
    // ---- vector maps (gfx/vecmaps.c, bmpmgr.c): 91:10-16 and 92:10/11 fill a vector or vector + distance map, 92:00/01 define the
    // ripples, 90:1A reads a picture through vector maps ----
    const auto find_vec = [&d](std::int32_t id) -> vecmap::Map* {
        const auto it = d.vecmaps.find(static_cast<Word>(id));
        return it == d.vecmaps.end() ? nullptr : &it->second;
    };
    const auto vec_code_text = [](int rc) -> const char* {
        switch (static_cast<std::uint32_t>(rc)) {
        case 0x80000003: return "is not a map of the mode this instruction fills";
        case 0x80000004: return "has the wrong height field size";
        case 0x80000005: return "gets an empty stretch range";
        case 0x80000006: return "gets a zero ripple period";
        case 0x80000008: return "gets a zero angle or radius";
        case 0x80000009: return "gets an unknown ripple type";
        case 0x8000000A: return "gets an unknown wipe type";
        default: return "failed";
        }
    };
    // the generators: pops `pops` values (the first pushed is the slot), runs the kernel on the slot's map, reports its code as a script error
    const auto vec_rand = std::make_shared<std::uint32_t>(0x2545F491u);
    const auto add_gen = [=, &r, &d](std::uint16_t id, const char* name, int pops, std::function<int(vecmap::Map&, const std::int32_t*)> run) {
        add(r, id, name, pops, 0, [=, &d](Ctx& c) {
            std::int32_t a[8] = {};
            for (int i = pops - 1; i >= 0; --i) a[i] = static_cast<std::int32_t>(c.pop());
            if (!c.ok()) return 0;
            vecmap::Map* m = find_vec(a[0]);
            if (!m) {
                c.fail(Errc::Range, std::string(name) + ": " + (exists(a[0]) ? "bitmap " + std::to_string(a[0]) + " is not a vector map" : "destination bitmap " + std::to_string(a[0]) + " does not exist"));
                return 0;
            }
            ++d.vecmap_epoch;
            if (const int rc = run(*m, a + 1); rc != 0) c.fail(Errc::Range, std::string(name) + ": bitmap " + std::to_string(a[0]) + " " + vec_code_text(rc));
            return 0;
        });
    };
    add_gen(0x9110, "vector map: stretch range", 5, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::stretch_range(m, a[0], a[1], a[2], a[3]); });
    add_gen(0x9111, "vector map: random", 2, [vec_rand](vecmap::Map& m, const std::int32_t* a) {
        return vecmap::random(m, a[0], [vec_rand] { *vec_rand = *vec_rand * 214013u + 2531011u; return (*vec_rand >> 16) & 0x7FFFu; });
    });
    add_gen(0x9112, "vector map: ripple", 6, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::ripple(m, a[0], a[1], a[2], a[3], a[4]); });
    add_gen(0x9113, "vector map: polar", 5, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::polar(m, a[0], a[1], a[2], a[3]); });
    add_gen(0x9114, "vector map: bend by angle", 5, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::bend_angle(m, a[0], a[1], a[2], a[3]); });
    add_gen(0x9115, "vector map: bend", 5, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::bend(m, a[0], a[1], a[2], a[3]); });
    add_gen(0x9116, "vector map: sine", 7, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::sine(m, a[0], a[1], a[2], a[3], a[4], a[5]); });
    add_gen(0x9210, "vector + distance map: radial", 5, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::radial(m, a[0], a[1], a[2], a[3]); });
    add_gen(0x9211, "vector + distance map: linear", 2, [](vecmap::Map& m, const std::int32_t* a) { return vecmap::linear(m, a[0]); });
    // 92:00 (no, period, amplitude, count, rings) and 92:01 (no, period, amplitude, fadeIn, fadeOut, count, rings): a ripple definition, numbers 0-7
    add(r, 0x9200, "define ripple", 5, 0, [&d](Ctx& c) {
        const auto rings = static_cast<std::int32_t>(c.pop()), count = static_cast<std::int32_t>(c.pop()), amp = static_cast<std::int32_t>(c.pop());
        const auto period = static_cast<std::int32_t>(c.pop()), no = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        ++d.vecmap_epoch;
        if (vecmap::define_ripple(d.ripples, no, period, amp, count, rings) != 0) c.fail(Errc::Range, "invalid ripple number " + std::to_string(no));
        return 0;
    });
    add(r, 0x9201, "define ripple with fades", 7, 0, [&d](Ctx& c) {
        const auto rings = static_cast<std::int32_t>(c.pop()), count = static_cast<std::int32_t>(c.pop());
        const auto fade_out = static_cast<std::int32_t>(c.pop()), fade_in = static_cast<std::int32_t>(c.pop());
        const auto amp = static_cast<std::int32_t>(c.pop()), period = static_cast<std::int32_t>(c.pop()), no = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        ++d.vecmap_epoch;
        if (vecmap::define_ripple_ex(d.ripples, no, period, amp, fade_in, fade_out, count, rings) != 0) c.fail(Errc::Range, "invalid ripple number " + std::to_string(no));
        return 0;
    });
    // 90:5B (object, x, y, bmp, map, rings, ripple, level, fade, priority): the picture read through the vector + distance map of its own size
    // and the ripple definition `ripple` (92:00) with `rings` rings; the level is the amplitude (a later level change moves it), the fade the
    // transparency it is drawn with (sprite.c: Sprite_SetRippleEx; the effect is forced to 1)
    add(r, 0x905B, "set ripple sprite", 10, 0, [=, &d](Ctx& c) {
        const Word priority = c.pop(), fade = c.pop();
        const auto level = static_cast<std::int32_t>(c.pop()), ripple = static_cast<std::int32_t>(c.pop()), rings = static_cast<std::int32_t>(c.pop());
        const auto map = static_cast<std::int32_t>(c.pop()), bmp = static_cast<std::int32_t>(c.pop());
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word id = c.pop();
        if (!c.ok()) return 0;
        if (!slot_ok(c, static_cast<Word>(bmp)) || !slot_ok(c, static_cast<Word>(map))) return 0;
        if (fade > 0x100 || priority > 0xffff) { c.fail(Errc::Range, "ripple sprite parameter out of range"); return 0; }
        const SlotInfo* bi = exists(bmp) ? d.slot(static_cast<Word>(bmp)) : nullptr;
        if (!bi) { c.fail(Errc::Range, "invalid bitmap " + std::to_string(bmp)); return 0; }
        if (bi->width == 0 || bi->height == 0) { c.fail(Errc::Range, "bitmap " + std::to_string(bmp) + " is empty"); return 0; }
        const vecmap::Map* vd = find_vec(map);
        if (!vd && !exists(map)) { c.fail(Errc::Range, "invalid vector + distance map " + std::to_string(map)); return 0; }
        if (!vd || vd->mode != vecmap::Mode::VecDist || vd->w != bi->width || vd->h != bi->height) { c.fail(Errc::Range, "vector + distance map " + std::to_string(map) + " does not fit bitmap " + std::to_string(bmp)); return 0; }
        if (rings == 0) { c.fail(Errc::Range, "invalid maximum distance 0"); return 0; }
        bool ok = false;
        if (vecmap::ripple_check(d.ripples, ok, ripple, 0, rings) != 0) { c.fail(Errc::Range, "ripple " + std::to_string(ripple) + " is not registered"); return 0; }
        if (!ok) { c.fail(Errc::Range, "ripple " + std::to_string(ripple) + " does not fit the map " + std::to_string(map)); return 0; }
        Sprite& s = d.sprite(id);
        s.slot = static_cast<Word>(bmp), s.slot2 = 0xFFFFFFFFu, s.mix_ratio = 0, s.level_route = -1, s.projected = false, s.xform = false;
        s.ripple_map = map, s.rings = rings, s.ripple_no = ripple, s.ripple_sel = 0;
        s.alpha = static_cast<Word>(level);
        s.x = x, s.y = y, s.blend = 1, s.fade = fade, s.priority = priority;
        return 0;
    });
    // 91:31 (object, hidden): any non-zero value keeps the object from being drawn (gfx/mgr/generic.c: Gfx_ObjSetHidden); an unknown handle is an error
    add(r, 0x9131, "set object hidden", 2, 0, [&d](Ctx& c) {
        const Word v = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = d.find_sprite(h);
        if (!s) { c.fail(Errc::Range, "no display object " + std::to_string(h)); return 0; }
        s->hidden = v != 0;
        return 0;
    });
    // 91:33 (object, fx, fy, fz): the 16.16 position and depth; with the snap switch (90:38 0x8000) x and y are first rounded to whole pixels
    // (only at depth 0 unless the snap mode is 1); the plane is placed at the integer position and scaled by the depth
    add(r, 0x9133, "set object fixed position", 4, 0, [&d](Ctx& c) {
        auto fz = static_cast<std::int32_t>(c.pop()), fy = static_cast<std::int32_t>(c.pop()), fx = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = d.find_sprite(h);
        if (!s) { c.fail(Errc::Range, "no display object " + std::to_string(h)); return 0; }
        if (s->snap && (fz == 0 || s->snap_mode == 1)) {
            fx = static_cast<std::int32_t>((static_cast<std::uint32_t>(fx) + 0x8000u) & 0xffff0000u);
            fy = static_cast<std::int32_t>((static_cast<std::uint32_t>(fy) + 0x8000u) & 0xffff0000u);
        }
        s->depth = fz;
        s->script_move_to(fx >> 16, fy >> 16);
        return 0;
    });
    // 91:37 (object, x, y, z) and 91:36 (object, x, y, z): the first and the second 16.16 offset added to the fixed position
    for (auto [id, which] : {std::pair{0x9137, 1}, {0x9136, 2}})
        add(r, static_cast<std::uint16_t>(id), "set object fixed position offset", 4, 0, [&d, which](Ctx& c) {
            const auto z = static_cast<std::int32_t>(c.pop()), y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
            const Word h = c.pop();
            if (!c.ok()) return 0;
            Sprite* s = d.find_sprite(h);
            if (!s) { c.fail(Errc::Range, "no display object " + std::to_string(h)); return 0; }
            std::int32_t* v = which == 2 ? s->fix_off2 : s->fix_off1;
            v[0] = x, v[1] = y, v[2] = z;
            return 0;
        });
    // 90:45 (bmp, vec1, vec2, level, amount): type 6, a screen-sized picture displaced by one vector map, or two (vec2 -1 = none) mixed by the level
    const auto screen_map = [&d, find_vec](std::int32_t id, vecmap::Mode mode) {
        const vecmap::Map* m = find_vec(id);
        return m && m->mode == mode && m->w == d.screen_width() && m->h == d.screen_height();
    };
    add(r, 0x9045, "background displace", 5, 0, [=, &d](Ctx& c) {
        const auto amount = static_cast<std::int32_t>(c.pop());
        const Word level = c.pop();
        const auto vec2 = static_cast<std::int32_t>(c.pop()), vec1 = static_cast<std::int32_t>(c.pop()), bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        fx_select(6, 0);
        if (!bg_usable(bmp, false)) { c.fail(Errc::Range, "background bitmap " + std::to_string(bmp) + " is unusable"); return 0; }
        if (!screen_map(vec1, vecmap::Mode::Vector)) { c.fail(Errc::Range, "vector map " + std::to_string(vec1) + " is missing or not screen sized"); return 0; }
        if (vec2 != -1 && !screen_map(vec2, vecmap::Mode::Vector)) { c.fail(Errc::Range, "vector map " + std::to_string(vec2) + " is missing or not screen sized"); return 0; }
        d.bg_fx.bmp[0] = bmp, d.bg_fx.bmp[1] = vec1, d.bg_fx.bmp[2] = vec2, d.bg_fx.amount = amount;
        d.sprite(kBgUpper).alpha = level;
        return 0;
    });
    // 90:47 (bmp, map, rings, ripple, level): type 8, the picture under the ripples of definition `ripple` (92:00) with `rings` rings, read
    // through the screen-sized vector + distance map; the level is the amplitude
    add(r, 0x9047, "background ripple", 5, 0, [=, &d](Ctx& c) {
        const Word level = c.pop();
        const auto ripple = static_cast<std::int32_t>(c.pop()), rings = static_cast<std::int32_t>(c.pop());
        const auto map = static_cast<std::int32_t>(c.pop()), bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        fx_select(8, 0);
        if (!bg_usable(bmp, false)) { c.fail(Errc::Range, "background bitmap " + std::to_string(bmp) + " is unusable"); return 0; }
        if (!screen_map(map, vecmap::Mode::VecDist)) { c.fail(Errc::Range, "vector + distance map " + std::to_string(map) + " is missing or not screen sized"); return 0; }
        if (rings == 0) { c.fail(Errc::Range, "invalid maximum distance 0"); return 0; }
        bool ok = false;
        if (vecmap::ripple_check(d.ripples, ok, ripple, 0, rings) != 0) { c.fail(Errc::Range, "ripple " + std::to_string(ripple) + " is not registered"); return 0; }
        if (!ok) { c.fail(Errc::Range, "ripple " + std::to_string(ripple) + " does not fit the map " + std::to_string(map)); return 0; }
        d.bg_fx.bmp[0] = bmp, d.bg_fx.bmp[1] = map, d.bg_fx.rings = rings, d.bg_fx.ripple_no = ripple, d.bg_fx.selector = 0;
        d.sprite(kBgUpper).alpha = level;
        return 0;
    });
    // 90:1A (dst, src, vec1, vec2, level, amount): src read through the vector map vec1, or the mix of vec1 and vec2 (-1 = none) by level / 256
    add(r, 0x901A, "displace image", 6, 0, [=, &d](Ctx& c) {
        const auto amount = static_cast<std::int32_t>(c.pop()), level = static_cast<std::int32_t>(c.pop());
        const auto vec2 = static_cast<std::int32_t>(c.pop()), vec1 = static_cast<std::int32_t>(c.pop());
        const auto src = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (!exists(dst)) { c.fail(Errc::Range, "invalid destination bitmap " + std::to_string(dst)); return 0; }
        if (!exists(src)) { c.fail(Errc::Range, "invalid source bitmap " + std::to_string(src)); return 0; }
        if (!find_vec(vec1) && !exists(vec1)) { c.fail(Errc::Range, "invalid vector map " + std::to_string(vec1)); return 0; }
        if (vec2 != -1 && !find_vec(vec2) && !exists(vec2)) { c.fail(Errc::Range, "invalid vector map " + std::to_string(vec2)); return 0; }
        const vecmap::Map* v1 = find_vec(vec1);
        const vecmap::Map* v2 = vec2 != -1 ? find_vec(vec2) : nullptr;
        const auto in = picture(src);
        if (!in) return 0; // other modes are not drawn
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *si = d.slot(static_cast<Word>(src));
        if (static_cast<std::uint32_t>(level) > 0x100) { c.fail(Errc::Range, "invalid effect level " + std::to_string(level)); return 0; }
        if (!v1 || v1->w != in->w || v1->h != in->h) { c.fail(Errc::Range, "bitmap " + std::to_string(vec1) + " is not a vector map for " + std::to_string(dst)); return 0; }
        if (vec2 != -1 && (!v2 || v2->w != in->w || v2->h != in->h)) { c.fail(Errc::Range, "bitmap " + std::to_string(vec2) + " is not a vector map for " + std::to_string(dst)); return 0; }
        if (di->format != si->format) { c.fail(Errc::Range, "bitmaps " + std::to_string(dst) + " and " + std::to_string(src) + " differ in mode"); return 0; }
        bgfx::Image out;
        if (vecmap::displace(out, *in, *v1, v2, level, amount) != 0) return 0;
        check(c, d.upload_slot(static_cast<Word>(dst), out.w, out.h, di->format, out.px));
        return 0;
    });
    // 91:19 (dst, x, y, src, cx, cy, angle, sx, sy, level, smooth): src scaled by (sx, sy) and rotated about its point (cx, cy) (all 16.16), which
    // lands on (x, y) of dst; every pixel of dst is replaced, the colours dimmed by the level (blit_xform.c: Blit_XformCopy). Pictures of two modes do nothing.
    add(r, 0x9119, "transform image copy", 11, 0, [=, &d](Ctx& c) {
        const auto smooth = static_cast<std::int32_t>(c.pop()), level = static_cast<std::int32_t>(c.pop());
        const auto sy = static_cast<std::int32_t>(c.pop()), sx = static_cast<std::int32_t>(c.pop()), angle = static_cast<std::int32_t>(c.pop());
        const auto cy = static_cast<std::int32_t>(c.pop()), cx = static_cast<std::int32_t>(c.pop()), src = static_cast<std::int32_t>(c.pop());
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (static_cast<std::uint32_t>(level) > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        if (!exists(dst)) { c.fail(Errc::Range, "invalid output bitmap " + std::to_string(dst)); return 0; }
        if (!exists(src)) { c.fail(Errc::Range, "invalid reference bitmap " + std::to_string(src)); return 0; }
        if (sx == 0 || sy == 0) { c.fail(Errc::Range, "invalid scale " + std::to_string(sx) + "," + std::to_string(sy)); return 0; }
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *si = d.slot(static_cast<Word>(src));
        if (di->format != si->format) return 0;
        auto from = picture(src);
        auto to = picture(dst);
        if (!from || !to) return 0;
        bgfx::xform_copy(*to, x << 16, y << 16, *from, cx, cy, angle, sx, sy, level, smooth != 0);
        check(c, d.upload_slot(static_cast<Word>(dst), to->w, to->h, di->format, to->px));
        return 0;
    });
    // 91:1B (dst, src, cx, cy, level): the gathering add of the screen-group transition: the RGB32 src drawn toward its centre by the 16.16 rates
    // (at most 1.0) and added to dst with saturation, attenuated by the level (blit_scale.c: Blit_GatherAdd)
    add(r, 0x911B, "gathering add", 5, 0, [=, &d](Ctx& c) {
        const Word level = c.pop(), cy = c.pop(), cx = c.pop();
        const auto src = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (!exists(dst)) { c.fail(Errc::Range, "invalid output bitmap " + std::to_string(dst)); return 0; }
        if (!exists(src)) { c.fail(Errc::Range, "invalid reference bitmap " + std::to_string(src)); return 0; }
        if (cx > 0x10000 || cy > 0x10000) { c.fail(Errc::Range, "invalid gathering rate"); return 0; }
        if (level > 0x100) { c.fail(Errc::Range, "invalid attenuation " + std::to_string(level)); return 0; }
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *si = d.slot(static_cast<Word>(src));
        if (di->format != SlotFormat::Bgrx || si->format != SlotFormat::Bgrx) return 0; // RGB32 on both sides, else nothing is drawn
        auto from = picture(src);
        auto to = picture(dst);
        if (!from || !to) return 0;
        bgfx::gather_add(*to, *from, cx, cy, level);
        check(c, d.upload_slot(static_cast<Word>(dst), to->w, to->h, di->format, to->px));
        return 0;
    });
    // 91:1D (dst, src, mode, colour, level): src into dst through mode 0 copy, 1 blend toward src XOR colour, 2 luminance tint, 3 blend toward
    // the colour (alpha kept), 4 add the colour (alpha kept), at the level (blit_filter.c); another mode is an error
    add(r, 0x911D, "colour mode image", 5, 0, [=, &d](Ctx& c) {
        const auto level = static_cast<std::int32_t>(c.pop());
        const Word colour = c.pop();
        const auto mode = static_cast<std::int32_t>(c.pop()), src = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (static_cast<std::uint32_t>(level) > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        if (!exists(dst)) { c.fail(Errc::Range, "invalid output bitmap " + std::to_string(dst)); return 0; }
        if (!exists(src)) { c.fail(Errc::Range, "invalid reference bitmap " + std::to_string(src)); return 0; }
        if (mode < 0 || mode > 4) { c.fail(Errc::Range, "invalid process type " + std::to_string(mode)); return 0; }
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *si = d.slot(static_cast<Word>(src));
        if (di->format != si->format || di->width != si->width || di->height != si->height) return 0;
        auto from = picture(src);
        if (!from) return 0;
        const bgfx::Image out = bgfx::colour_mode(*from, mode, colour, level);
        check(c, d.upload_slot(static_cast<Word>(dst), out.w, out.h, di->format, out.px));
        return 0;
    });
    // 91:1E (dst, gray, x, y): the gray bitmap as the alpha of the ARGB32 dst: pixel (px, py) takes gray pixel (px + x, py + y) over the overlap
    // (blit_xform.c: Blit_ApplyMask); the alpha already there is multiplied by it
    add(r, 0x911E, "apply gray as alpha", 4, 0, [=, &d](Ctx& c) {
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const auto gray = static_cast<std::int32_t>(c.pop()), dst = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (!exists(dst)) { c.fail(Errc::Range, "invalid target bitmap " + std::to_string(dst)); return 0; }
        if (!exists(gray)) { c.fail(Errc::Range, "invalid reference bitmap " + std::to_string(gray)); return 0; }
        const SlotInfo *di = d.slot(static_cast<Word>(dst)), *gi = d.slot(static_cast<Word>(gray));
        if (gi->format != SlotFormat::Gray8) { c.fail(Errc::Range, "bitmap " + std::to_string(gray) + " is not grayscale"); return 0; }
        if (di->format != SlotFormat::Bgra) return 0; // an RGB32 destination is left alone
        auto to = picture(dst);
        auto mask = d.read_slot(static_cast<Word>(gray));
        if (!to || !mask) return 0;
        bgfx::apply_gray(*to, *mask, gi->width, gi->height, x, y);
        check(c, d.upload_slot(static_cast<Word>(dst), to->w, to->h, di->format, to->px));
        return 0;
    });
    // 90:0A, 0B, 0E, 0F: settings the original stores for its loader and glyph cache (present mode of a wait, the bitmap manager's option,
    // a glyph cache request, the key colour of 90:14); they change no picture here
    for (const auto& [id, pops] : {std::pair{0x900Au, 2}, {0x900Bu, 1}, {0x900Eu, 5}, {0x900Fu, 1}})
        add(r, static_cast<std::uint16_t>(id), "loader setting", pops, 0, [pops](Ctx& c) {
            for (int i = 0; i < pops; ++i) c.pop();
            return 0;
        });

    // ---- tile maps (90:70-7A; gfx/map.c, gfx/mgr/maps.c): eight of them, handle 0xA0000000 | index ----
    constexpr Word kMapBase = 0xA0000000u;
    const auto map_of = [&d](Ctx& c, Word h) -> Display::MapData* {
        const auto it = d.maps.find(h);
        if (it == d.maps.end()) { c.fail(Errc::Range, "invalid map handle " + std::to_string(h)); return nullptr; }
        return &it->second;
    };
    add(r, 0x9070, "create map", 0, 1, [&d](Ctx& c) {
        for (Word i = 0; i < 8; ++i)
            if (!d.maps.count(kMapBase | i)) {
                Sprite& s = d.sprite(kMapBase | i);
                s = Sprite{};
                s.visible = false, s.slot = 0xFFFFFFFFu;
                d.maps[kMapBase | i] = {};
                c.push(kMapBase | i);
                return 0;
            }
        c.fail(Errc::Range, "too many maps (the limit is 8)");
        return 0;
    });
    add(r, 0x9071, "delete map", 1, 0, [&d, map_of](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok() || !map_of(c, h)) return 0;
        d.maps.erase(h);
        d.remove_sprite(h);
        return 0;
    });
    add(r, 0x9074, "show map", 2, 0, [&d, map_of](Ctx& c) {
        const Word f = c.pop(), h = c.pop();
        if (!c.ok() || !map_of(c, h)) return 0;
        d.set_visible(h, f != 0);
        return 0;
    });
    // 90:75 (map, x, y, chipset, effect, level, priority): the chipset, which must hold at least one tile each way; then the placement
    add(r, 0x9075, "set map", 7, 0, [&d, map_of](Ctx& c) {
        const Word prio = c.pop(), level = c.pop(), effect = c.pop();
        const auto bmp = static_cast<std::int32_t>(c.pop()), y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (prio > 0xffff || level > 0x100 || !blend_mode_supported(effect)) { c.fail(Errc::Range, "map parameter out of range or unsupported blend mode"); return 0; }
        Display::MapData* md = map_of(c, h);
        if (!md) return 0;
        const SlotInfo* si = bmp >= 0 && bmp < 0x4000 ? d.slot(static_cast<Word>(bmp)) : nullptr;
        if (!md->sized || !si || si->width < static_cast<Word>(md->chip_w) || si->height < static_cast<Word>(md->chip_h) ||
            (si->format != SlotFormat::Bgra && si->format != SlotFormat::Bgrx)) {
            c.fail(Errc::Range, "bitmap " + std::to_string(bmp) + " does not fit the map's tiles");
            return 0;
        }
        md->chipset = bmp, ++md->revision;
        Sprite& s = d.sprite(h);
        s.x = x, s.y = y, s.blend = effect, s.alpha = level, s.priority = prio;
        return 0;
    });
    // 90:76 (map, cols, rows, tile w, tile h): the view in tiles (at most 0x400 x 0x300 pixels) and the tile size (at most 0x100 x 0x100)
    add(r, 0x9076, "set map size", 5, 0, [&d, map_of](Ctx& c) {
        const auto chip_h = static_cast<std::int32_t>(c.pop()), chip_w = static_cast<std::int32_t>(c.pop());
        const auto rows = static_cast<std::int32_t>(c.pop()), cols = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Display::MapData* md = map_of(c, h);
        if (!md) return 0;
        if (chip_w <= 0 || chip_w > 0x100 || chip_h <= 0 || chip_h > 0x100) { c.fail(Errc::Range, "invalid tile size " + std::to_string(chip_w) + "x" + std::to_string(chip_h)); return 0; }
        if (cols <= 0 || cols > 0x400 / chip_w || rows <= 0 || rows > 0x300 / chip_h) { c.fail(Errc::Range, "invalid map view size " + std::to_string(cols) + "x" + std::to_string(rows)); return 0; }
        md->cols = cols, md->rows = rows, md->chip_w = chip_w, md->chip_h = chip_h;
        md->sized = true, md->chipset = -1, md->view_valid = false, md->col = md->row = md->off_x = md->off_y = -1; // the chipset and the view are dropped
        ++md->revision;
        return 0;
    });
    // 90:78 (map, w, h, data): the terrain, w x h 16-bit tile numbers in script memory, row by row; the view is forgotten
    add(r, 0x9078, "set map terrain", 4, 0, [&d, map_of](Ctx& c) {
        const Word data = c.pop(), hgt = c.pop(), w = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        Display::MapData* md = map_of(c, h);
        if (!md) return 0;
        if (w == 0 || hgt == 0 || std::uint64_t{w} * hgt > 0x1000000u) { c.fail(Errc::Range, "invalid terrain size " + std::to_string(w) + "x" + std::to_string(hgt)); return 0; }
        std::vector<std::uint16_t> terrain(std::size_t{w} * hgt);
        for (std::size_t i = 0; i < terrain.size(); ++i) terrain[i] = static_cast<std::uint16_t>(c.read(data + static_cast<Word>(2 * i), 2, false));
        if (!c.ok()) return 0;
        md->terrain = std::move(terrain), md->terr_w = static_cast<std::int32_t>(w), md->terr_h = static_cast<std::int32_t>(hgt), md->has_terrain = true;
        md->view_valid = false, md->col = md->row = md->off_x = md->off_y = -1;
        ++md->revision;
        return 0;
    });
    // 90:79 (map, col, row, offset x, offset y, wrap): the terrain cell at the window's top left and the pixel offset inside it (less than a tile)
    add(r, 0x9079, "set map view", 6, 0, [&d, map_of](Ctx& c) {
        const Word wrap = c.pop(), off_y = c.pop(), off_x = c.pop(), row = c.pop(), col = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        Display::MapData* md = map_of(c, h);
        if (!md) return 0;
        if (!md->sized || !md->has_terrain || col >= static_cast<Word>(md->terr_w) || row >= static_cast<Word>(md->terr_h) ||
            off_x >= static_cast<Word>(md->chip_w) || off_y >= static_cast<Word>(md->chip_h)) {
            c.fail(Errc::Range, "invalid map view position " + std::to_string(static_cast<std::int32_t>(col)) + "," + std::to_string(static_cast<std::int32_t>(row)));
            return 0;
        }
        md->col = static_cast<std::int32_t>(col), md->row = static_cast<std::int32_t>(row);
        md->off_x = static_cast<std::int32_t>(off_x), md->off_y = static_cast<std::int32_t>(off_y);
        md->wrap = wrap != 0, md->view_valid = true;
        ++md->revision;
        return 0;
    });
    // 90:7A (map, tile): every cell showing the tile is redrawn from the chipset after the script repainted it there
    add(r, 0x907A, "invalidate map tile", 2, 0, [&d, map_of](Ctx& c) {
        c.pop();
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Display::MapData* md = map_of(c, h);
        if (!md) return 0;
        if (!md->sized) { c.fail(Errc::Range, "the map is not initialised"); return 0; }
        ++md->revision;
        return 0;
    });
}

} // namespace obgi
