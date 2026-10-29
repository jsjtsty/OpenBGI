#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <cstdlib>
#include <cstdio>
#include "bindings/graphics_subops.hpp"

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

    // ---- image slots ----
    add(r, 0x9010, "load image", 3, 0, [&host, &d](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        Word slot = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:10 slot %u <- %s/%s\n", slot, prefix.c_str(), name.c_str());
        auto img = host.images->load(prefix, name);
        if (!img) {
            c.fail(img.error().code, "\"" + prefix + "/" + name + "\": " + img.error().message);
            return 0;
        }
        check(c, d.upload_slot(slot, img->width, img->height, img->format, img->pixels));
        return 0; // the original may go asynchronous (rc 2); loading here is always synchronous
    });
    add(r, 0x9011, "create image", 4, 0, [&d](Ctx& c) {
        Word format = c.pop(), h = c.pop(), w = c.pop(), slot = c.pop();
        if (!c.ok() || !slot_ok(c, slot)) return 0;
        if (format > 3) {
            c.fail(Errc::Range, "bad image format " + std::to_string(format));
            return 0;
        }
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
        s.visible = real && visible;
    };
    add(r, 0x9043, "background two pictures", 9, 0, [bg, bg_picture, &d](Ctx& c) {
        const Word level = c.pop();
        c.pop(), c.pop(); // wipe parameter, wipe map
        const auto bmp2 = static_cast<std::int32_t>(c.pop());
        const auto y2 = static_cast<std::int32_t>(c.pop()), x2 = static_cast<std::int32_t>(c.pop());
        const auto bmp1 = static_cast<std::int32_t>(c.pop());
        const auto y1 = static_cast<std::int32_t>(c.pop()), x1 = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "alpha out of range"); return 0; }
        if (std::getenv("OBGI_DBGBG")) {
            const SlotInfo* si = bmp1 >= 0 ? d.slot(static_cast<Word>(bmp1)) : nullptr;
            std::fprintf(stderr, "90:43 upper %d (%ux%u) at %d,%d lower %d at %d,%d level %u\n", bmp1, si ? si->width : 0, si ? si->height : 0, x1, y1, bmp2, x2, y2, level);
        }
        bg->type = 4;
        bg_picture(kBgLower, bmp2, x2, y2, 0, bg->visible);
        bg_picture(kBgUpper, bmp1, x1, y1, level, bg->visible);
        return 0;
    });
    // 90:40 (bmp): the plain background picture
    add(r, 0x9040, "background picture", 1, 0, [bg, bg_picture](Ctx& c) {
        const auto bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        bg->type = 1;
        bg_picture(kBgLower, bmp, 0, 0, 0, bg->visible);
        bg_picture(kBgUpper, -1, 0, 0, 0, false);
        return 0;
    });
    // 90:47 (bmp, vector map, rings, ripple, level): the picture under ripples; the ripple is not drawn, the picture is
    add(r, 0x9047, "background ripple", 5, 0, [bg, bg_picture](Ctx& c) {
        c.pop(), c.pop(), c.pop(), c.pop();
        const auto bmp = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        bg->type = 8;
        bg_picture(kBgLower, bmp, 0, 0, 0, bg->visible);
        bg_picture(kBgUpper, -1, 0, 0, 0, false);
        return 0;
    });
    // 90:4A (front, back, style, level, link): the mosaic transition from front to back; cross-faded plainly instead
    add(r, 0x904A, "background flip", 5, 0, [bg, bg_picture](Ctx& c) {
        c.pop(); // link
        const Word level = c.pop();
        c.pop(); // style
        const auto back = static_cast<std::int32_t>(c.pop()), front = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        bg->type = 11;
        bg_picture(kBgLower, back, 0, 0, 0, bg->visible);
        bg_picture(kBgUpper, front, 0, 0, level > 0x100 ? 0x100 : level, bg->visible);
        return 0;
    });
    add(r, 0x904D, "background type", 0, 1, [bg](Ctx& c) { c.push(bg->type); return 0; });
    add(r, 0x904C, "background show", 2, 0, [bg, &d](Ctx& c) {
        c.pop(); // draw mode
        bg->visible = c.pop() != 0;
        if (!c.ok()) return 0;
        for (Word id : {kBgLower, kBgUpper})
            if (Sprite* s = d.find_sprite(id)) s->visible = bg->visible && s->slot != 0;
        return 0;
    });
    // 92:12 (slot, w, h): sets the base size of a slot (the design size of its image); 1 for a valid slot number.
    // 92:16 (out, slot): reads it into two words at out; until set, the size of the image in the slot (0, 0 without one).
    auto base_sizes = std::make_shared<std::map<Word, std::pair<std::int32_t, std::int32_t>>>();
    add(r, 0x9212, "set image base size", 3, 1, [base_sizes](Ctx& c) {
        const auto h = static_cast<std::int32_t>(c.pop()), w = static_cast<std::int32_t>(c.pop());
        const Word slot = c.pop();
        if (!c.ok()) return 0;
        if (slot <= kMaxSlot) (*base_sizes)[slot] = {w, h};
        c.push(slot <= kMaxSlot ? 1 : 0);
        return 0;
    });
    add(r, 0x9216, "image base size", 2, 1, [&d, base_sizes](Ctx& c) {
        const Word slot = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        if (slot > kMaxSlot) { c.push(0); return 0; }
        std::int32_t w = 0, h = 0;
        if (auto it = base_sizes->find(slot); it != base_sizes->end()) w = it->second.first, h = it->second.second;
        else if (const SlotInfo* info = d.slot(slot)) w = static_cast<std::int32_t>(info->width), h = static_cast<std::int32_t>(info->height);
        c.write(out, static_cast<Word>(w), 4);
        c.write(out + 4, static_cast<Word>(h), 4);
        c.push(1);
        return 0;
    });
    add(r, 0x9012, "free image", 1, 1, [&d](Ctx& c) {
        Word slot = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "90:12 free slot %u\n", slot);
        c.push(d.free_slot(slot) ? 1 : 0); // verified in the real game: 1 freed, 0 no such slot
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
            std::fprintf(stderr, "90:5C h=%X fx=%d fy=%d fz=%d bmp1=%d bmp2=%d mix=%u route=%d ox=%d oy=%d angle=%d dist=%d projPos=%d smooth=%d effect=%u level=%u prio=%u\n",
                         v[16], static_cast<int>(v[15]), static_cast<int>(v[14]), static_cast<int>(v[13]), static_cast<int>(v[12]), static_cast<int>(v[11]), v[10], static_cast<int>(v[9]),
                         static_cast<int>(v[8]), static_cast<int>(v[7]), static_cast<int>(v[6]), static_cast<int>(v[5]), static_cast<int>(v[4]), static_cast<int>(v[3]), v[2], v[1], v[0]);
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
        s->slot = v[12];
        s->x = static_cast<std::int32_t>(d.screen_width() / 2) + (static_cast<std::int32_t>(v[15]) >> 16);
        s->y = static_cast<std::int32_t>(d.screen_height() / 2) + (static_cast<std::int32_t>(v[14]) >> 16);
        s->alpha = v[1], s->priority = v[0];
        if (blend_mode_supported(v[2])) s->blend = v[2]; // v[2] is the effect, not a visibility flag
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
        else s->alpha = alpha;
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
        if (Sprite* s = object(c, d, id)) s->move_to(x, y);
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
        if (Sprite* s = object(c, d, id)) s->enabled = value != 0;
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
    add(r, 0x9038, "set object property", 4, 0, [&d](Ctx& c) {
        Word b = c.pop(), a = c.pop(), prop = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = object(c, d, id);
        if (!s) return 0;
        switch (prop) {
        case 0: s->move_to(static_cast<std::int32_t>(a), static_cast<std::int32_t>(b)); break;
        case 1:
            if (!blend_mode_supported(a)) c.fail(Errc::Unsupported, "blend mode " + std::to_string(a) + " is not implemented");
            else s->blend = a;
            break;
        case 2:
            if (a > 0x100) c.fail(Errc::Range, "alpha out of range");
            else s->alpha = a;
            break;
        // properties the engine stores without any effect we render yet (analysis/显示对象属性.md)
        case 0xC0: case 0xC1: case 0xC4: case 0xC5: case 0x8000: case 0x8001: case 0x8100: case 0xFFFF:
        case 0x7FFF0000: case 0x7FFFFFFF:
            break;
        // parameters of the effect object types (0x41 and 0x80..0x83 apply to types 2, 5 and 6; 0x8F to all): the original only
        // stores them for the effect; the scripts so far reset them to 0 (analysis/显示对象属性.md)
        case 0x41: case 0x80: case 0x81: case 0x82: case 0x83: case 0x8F:
            break;
        // 0x40 origin (x, y), 0x41 rotation, 0x42 scale (x, y; 16.16, 0 = 1) of the picture types: only the identity is drawn so far
        case 0x40: case 0x42: case 0x43: case 0x60: {
            static std::set<Word> shown;
            const bool identity = prop == 0x42 ? (a == 0x10000 || a == 0) && (b == 0x10000 || b == 0) : prop == 0x40 || prop == 0x43 || prop == 0x60;
            if (!identity && shown.insert(prop).second) std::fprintf(stderr, "90:38: property 0x%X (a=%u b=%u) is stored but not drawn\n", prop, a, b);
            break;
        }
        case 0x8101:
            if (a > 1) c.fail(Errc::Range, "property 0x8101 takes 0 or 1");
            break;
        default: { // subclasses override the extension hook; not modelled yet, so the value is dropped (noted once per id)
            static std::set<Word> noted;
            if (std::getenv("OBGI_DBGOBJ")) std::fprintf(stderr, "90:38 object %X slot %u prop 0x%X a=%u b=%u\n", id, s->slot, prop, a, b);
            if (noted.insert(prop).second) std::fprintf(stderr, "90:38: object property 0x%X (a=%u b=%u) is not modelled\n", prop, a, b);
            break;
        }
        }
        return 0;
    });
}

} // namespace obgi
