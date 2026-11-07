#include "bindings/sequence_subops.hpp"
#include <utility>

namespace obgi {

void register_sequence_subops(vm::SubopRegistry& r, SequenceService& sequences, ArcResourceFinder& resources) {
    using vm::Ctx;
    using vm::Word;
    r.add({0x8053, "defer sequence frame decode", 0, 0, [&sequences](Ctx&) {
        sequences.defer_next_decode = true;
        return 0;
    }});
    r.add({0x90F4, "load sequence", 4, 1, [&sequences, &resources](Ctx& c) {
        const auto name = c.cstring(c.pop()), arc = c.cstring(c.pop());
        const Word info_address = c.pop(), id_address = c.pop();
        if (!c.ok()) return 0;
        auto bytes = resources.read(arc, name);
        if (!bytes) { c.push(1); return 0; }
        auto info = sequences.load(std::move(*bytes));
        if (!info) { c.push(2); return 0; }
        c.write(id_address, info->id, 4);
        const Word words[] = {info->width, info->height, info->mode, info->rate, info->frames};
        for (Word i = 0; i < 5; ++i) c.write(info_address + 4 * i, words[i], 4);
        c.push(0);
        return 0;
    }});
    r.add({0x90F5, "free sequence", 1, 1, [&sequences](Ctx& c) {
        const Word id = c.pop();
        if (c.ok()) c.push(sequences.release(id) ? 0 : 3);
        return 0;
    }});
    r.add({0x90F6, "decode sequence frame", 3, 1, [&sequences](Ctx& c) {
        const Word frame = c.pop(), id = c.pop(), bitmap = c.pop();
        if (!c.ok()) return 0;
        auto request = sequences.decode(bitmap, id, frame, std::exchange(sequences.defer_next_decode, false));
        if (!request.operation) { c.push(request.status); return 0; }
        vm::WaitObject wait;
        wait.kind = vm::WaitObject::Kind::Poll;
        wait.poll = [&sequences, op = std::move(request.operation)](const vm::WaitContext&) -> Result<vm::WaitUpdate> {
            const auto result = sequences.poll(*op);
            return result ? vm::WaitUpdate::complete({*result}) : vm::WaitUpdate::pending();
        };
        c.suspend(std::move(wait), 1);
        return static_cast<int>(vm::StayThread);
    }, 1});
    r.add({0x90F7, "clone sequence", 2, 1, [&sequences](Ctx& c) {
        const Word address = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        auto result = sequences.clone(id);
        if (!result.status) c.write(address, result.id, 4);
        c.push(result.status);
        return 0;
    }});
}

} // namespace obgi
