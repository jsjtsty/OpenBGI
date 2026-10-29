#include "bindings/sequence_subops.hpp"

#include <cstring>
#include <map>
#include <memory>

#include "formats/bm.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

// A loaded BF_Movie sequence (90:F4..F7, analysis/BM格式.md): the file and a decoder positioned at some frame. A clone shares the file.
struct Sequence {
    std::shared_ptr<std::vector<std::uint8_t>> file;
    std::optional<BmDecoder> decoder;
    std::uint32_t next_frame = 0; // the first frame the decoder has not decoded yet
    Word clone_of = 0, clone = 0;
};

struct SequenceTable {
    std::map<Word, Sequence> by_no;
    Word counter = 0;
};

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

std::uint32_t header_mode(const Sequence& s) { return load_u32le(s.file->data() + 0x20); }

} // namespace

void register_sequence_subops(vm::SubopRegistry& r, Display* display, ArcResourceFinder& resources) {
    auto table = std::make_shared<SequenceTable>();
    ArcResourceFinder* finder = &resources;

    // 90:F4 (&no, &info, archive, name) -> result: loads the sequence; writes its number to *no and width, height, pixel mode,
    // frame rate and frame count to the five words at info. 0 ok, 1 the file cannot be read, 2 it is not a movie.
    add(r, 0x90F4, "load sequence", 4, 1, [table, finder](Ctx& c) {
        const std::string name = c.cstring(c.pop()), arc = c.cstring(c.pop());
        const Word info = c.pop(), out_no = c.pop();
        if (!c.ok()) return 0;
        auto bytes = finder->read(arc, name);
        if (!bytes) { c.push(1); return 0; }
        Sequence s;
        s.file = std::make_shared<std::vector<std::uint8_t>>(std::move(*bytes));
        auto dec = BmDecoder::open(*s.file);
        if (!dec) { c.push(2); return 0; }
        s.decoder = std::move(*dec);
        const Word no = ++table->counter;
        const Word words[5] = {s.decoder->width(), s.decoder->height(), header_mode(s), s.decoder->rate(), s.decoder->frame_count()};
        c.write(out_no, no, 4);
        for (int i = 0; i < 5; ++i) c.write(info + 4 * Word(i), words[i], 4);
        table->by_no.emplace(no, std::move(s));
        c.push(0);
        return 0;
    });
    // 90:F5 (no) -> result: releases the sequence; 0 ok, 3 no such sequence
    add(r, 0x90F5, "free sequence", 1, 1, [table](Ctx& c) {
        const Word no = c.pop();
        if (!c.ok()) return 0;
        auto it = table->by_no.find(no);
        if (it == table->by_no.end()) { c.push(3); return 0; }
        if (it->second.clone_of) if (auto o = table->by_no.find(it->second.clone_of); o != table->by_no.end()) o->second.clone = 0;
        if (it->second.clone) if (auto o = table->by_no.find(it->second.clone); o != table->by_no.end()) o->second.clone_of = 0;
        table->by_no.erase(it);
        c.push(0);
        return 0;
    });
    // 90:F6 (bitmap, no, frame) -> result: decodes a frame into a slot of the movie's size and pixel mode.
    // 0 ok, 3 no such sequence, 4 no such frame, 5 the slot does not match, 8 the decoder failed.
    add(r, 0x90F6, "decode sequence frame", 3, 1, [table, display](Ctx& c) {
        const Word frame = c.pop(), no = c.pop(), bmp = c.pop();
        if (!c.ok() || !display) return 0;
        auto it = table->by_no.find(no);
        if (it == table->by_no.end()) { c.push(3); return 0; }
        Sequence& s = it->second;
        if (frame >= s.decoder->frame_count()) { c.push(4); return 0; }
        const SlotInfo* slot = display->slot(bmp);
        const std::uint32_t mode = header_mode(s);
        if (!slot || slot->width != s.decoder->width() || slot->height != s.decoder->height() ||
            slot->format != (mode == 2 ? SlotFormat::Bgra : SlotFormat::Bgrx)) {
            c.push(5);
            return 0;
        }
        if (frame + 1 < s.next_frame) { // frames are deltas on the canvas: start again from the first one
            auto dec = BmDecoder::open(*s.file);
            if (!dec) { c.push(8); return 0; }
            s.decoder = std::move(*dec);
            s.next_frame = 0;
        }
        while (s.next_frame <= frame) {
            if (auto d = s.decoder->decode_frame(s.next_frame); !d) { c.push(8); return 0; }
            ++s.next_frame;
        }
        const ByteView canvas = s.decoder->canvas();
        std::vector<std::uint32_t> px(canvas.size() / 4);
        std::memcpy(px.data(), canvas.data(), px.size() * 4);
        if (auto up = display->upload_slot(bmp, slot->width, slot->height, slot->format, px); !up) { c.push(8); return 0; }
        c.push(0);
        return 0;
    });
    // 90:F7 (no, &out) -> result: a second number sharing the file; 0 ok, 3 no such sequence, 7 it has a clone already
    add(r, 0x90F7, "clone sequence", 2, 1, [table](Ctx& c) {
        const Word out = c.pop(), no = c.pop();
        if (!c.ok()) return 0;
        auto it = table->by_no.find(no);
        if (it == table->by_no.end()) { c.push(3); return 0; }
        if (it->second.clone) { c.push(7); return 0; }
        Sequence copy;
        copy.file = it->second.file;
        auto dec = BmDecoder::open(*copy.file);
        if (!dec) { c.push(8); return 0; }
        copy.decoder = std::move(*dec);
        copy.clone_of = no;
        const Word cno = ++table->counter;
        it->second.clone = cno;
        table->by_no.emplace(cno, std::move(copy));
        c.write(out, cno, 4);
        c.push(0);
        return 0;
    });
}

} // namespace obgi
