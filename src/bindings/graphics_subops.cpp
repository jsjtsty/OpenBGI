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
    add(r, 0x901F, "crop image", 6, 0, [&d](Ctx& c) {
        Word h = c.pop(), w = c.pop();
        auto y = static_cast<std::int32_t>(c.pop());
        auto x = static_cast<std::int32_t>(c.pop());
        Word src = c.pop(), dst = c.pop();
        if (!c.ok() || !slot_ok(c, dst)) return 0;
        const SlotInfo* s = d.slot(src);
        if (!s || w == 0 || h == 0) {
            c.fail(Errc::Range, "bad crop source or size");
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
    // 90:5C (17 values, popped top first: priority, alpha, visible, ?, ?, focal distance, 5 unknowns, second slot (-1 = none), image slot,
    // ?, y, x, object): turns an object into a perspective-projected plane showing the image slot. The position is the
    // plane's top-left corner relative to the screen centre in 16.16 fixed point. Only the flat, untransformed case is
    // modelled: the image is drawn at that position at full size (checked against the original's arguments).
    add(r, 0x905C, "projected plane", 17, 0, [&d](Ctx& c) {
        Word v[17];
        for (Word& x : v) x = c.pop();
        if (!c.ok()) return 0;
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
        s->alpha = v[1], s->priority = v[0], s->visible = v[2] != 0;
        return 0;
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
        if (Sprite* s = object(c, d, id)) s->visible = value != 0;
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
        if (Sprite* s = object(c, d, id)) s->x = x, s->y = y;
        return 0;
    });
    add(r, 0x9034, "set object factor", 2, 0, [&d](Ctx& c) {
        Word value = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (!object(c, d, id)) return 0;
        if (value > 0x100) c.fail(Errc::Range, "factor out of range"); // stored by the engine, no visible effect yet
        return 0;
    });
    add(r, 0x9038, "set object property", 4, 0, [&d](Ctx& c) {
        Word b = c.pop(), a = c.pop(), prop = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        Sprite* s = object(c, d, id);
        if (!s) return 0;
        switch (prop) {
        case 0: s->x = static_cast<std::int32_t>(a), s->y = static_cast<std::int32_t>(b); break;
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
        case 0x8101:
            if (a > 1) c.fail(Errc::Range, "property 0x8101 takes 0 or 1");
            break;
        default: { // subclasses override the extension hook; not modelled yet, so the value is dropped (noted once per id)
            static std::set<Word> noted;
            if (noted.insert(prop).second) std::fprintf(stderr, "90:38: object property 0x%X (a=%u b=%u) is not modelled\n", prop, a, b);
            break;
        }
        }
        return 0;
    });
}

} // namespace obgi
