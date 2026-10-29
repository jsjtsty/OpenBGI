#include <cstdio>
#include <cstdlib>
#include <algorithm>

#include "vm/vm.hpp"

namespace obgi::vm {

namespace {

void add(SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

void fail_result(Ctx& c, const Error& e) { c.fail(e.code, e.message); }

} // namespace

void register_core_subops(SubopRegistry& r) {
    // ---- program blocks ----
    // (size) -> address: allocates script memory
    add(r, 0x8020, "allocate", 1, 1, [](Ctx& c) {
        const Word size = c.pop();
        if (!c.ok()) return 0;
        auto off = c.vm.allocator.allocate(c.vm.heap, size);
        if (!off) {
            c.fail(off.error().code, "memory allocation of " + std::to_string(size) + " bytes failed");
            return 0;
        }
        c.push(make_addr(Tag::Heap, *off));
        return 0;
    });
    // (address) -> 1: frees an allocation; address 0 does nothing; anything else that is not a live block is an error
    add(r, 0x8021, "free", 1, 1, [](Ctx& c) {
        const Word addr = c.pop();
        if (!c.ok()) return 0;
        if (addr != 0) {
            const bool heap_addr = (addr >> kTagShift) == static_cast<Word>(Tag::Heap);
            if (!heap_addr || !c.vm.allocator.release(c.vm.heap, addr & kOffsetMask)) {
                c.fail(Errc::Range, "freeing an address that was not allocated");
                return 0;
            }
        }
        c.push(1);
        return 0;
    });
    add(r, 0x8040, "load program", 2, 1, [](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string arc = c.cstring(c.pop());
        if (!c.ok()) return 0;
        auto start = c.vm.load_module(c.t, arc, name);
        if (!start) fail_result(c, start.error());
        else c.push(*start);
        return 0;
    });
    // pushes the number of blocks still loaded, not counting the thread's first one (checked against the original)
    add(r, 0x8041, "unload program", 0, 1, [](Ctx& c) {
        if (auto u = c.vm.unload_module(c.t); !u) fail_result(c, u.error());
        else c.push(static_cast<Word>(c.t.modules.empty() ? 0 : c.t.modules.size() - 1));
        return 0;
    });

    // ---- threads ----
    add(r, 0x8044, "create thread", 5, 1, [](Ctx& c) {
        Word stack = c.pop(), prog = c.pop(), slots = c.pop();
        std::string name = c.cstring(c.pop());
        std::string arc = c.cstring(c.pop());
        if (!c.ok()) return 0;
        ThreadConfig cfg;
        cfg.stack_size = std::max<Word>(stack, 0x100);
        cfg.expr_slots = std::max<Word>(slots, 1);
        cfg.program_capacity = prog;
        auto t = c.vm.spawn(cfg, arc, name);
        if (!t) fail_result(c, t.error());
        else c.push((*t)->id);
        return 0;
    });
    add(r, 0x8046, "current thread id", 0, 1, [](Ctx& c) {
        c.push(c.t.id);
        return 0;
    });
    add(r, 0x8047, "thread exists", 1, 1, [](Ctx& c) {
        c.push(c.vm.find(c.pop()) != nullptr);
        return 0;
    });

    // ---- thread messages ----
    add(r, 0x8048, "send message", 2, 0, [](Ctx& c) {
        Word msg = c.pop(), id = c.pop();
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "80:48 from t%u to id %u msg %X\n", c.t.id, id, msg);
        if (id == 0) return 0; // the real game's root thread (id 0) takes messages and does nothing with them
        Thread* target = c.vm.find(id);
        if (!c.ok()) return 0;
        if (!target) c.fail(Errc::Range, "send to missing thread " + std::to_string(id));
        else target->queue.push_back(msg);
        return 0;
    });
    add(r, 0x8049, "receive message", 1, 1, [](Ctx& c) {
        Word addr = c.pop();
        if (c.t.queue.empty()) {
            c.push(0);
            return 0;
        }
        c.write(addr, c.t.queue.front(), 4);
        c.t.queue.pop_front();
        c.push(1);
        return 0;
    });
    add(r, 0x804A, "send messages", 3, 0, [](Ctx& c) {
        Word arr = c.pop(), n = c.pop(), id = c.pop(); // pop order from the original: array address, count, thread
        if (id == 0) return 0; // see 80:48
        Thread* target = c.vm.find(id);
        if (!c.ok()) return 0;
        if (!target) {
            c.fail(Errc::Range, "send to missing thread " + std::to_string(id));
            return 0;
        }
        for (Word k = 0; k < n && c.ok(); ++k) {
            target->queue.push_back(static_cast<Word>(c.read(arr + 4 * k, 4, false)));
            if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "80:4A from t%u to id %u [%u]=%X\n", c.t.id, id, k, target->queue.back());
        }
        return 0;
    });
    add(r, 0x804B, "receive messages", 2, 1, [](Ctx& c) {
        Word addr = c.pop(), limit = c.pop(), k = 0; // pop order from the original: buffer address, then the count
        while (!c.t.queue.empty() && k < limit && c.ok()) {
            c.write(addr + 4 * k, c.t.queue.front(), 4);
            c.t.queue.pop_front();
            ++k;
        }
        c.push(k);
        return 0;
    });

    // ---- scheduling ----
    add(r, 0x805E, "switch thread", 1, 0, [](Ctx& c) {
        c.vm.switch_target = c.pop();
        return static_cast<int>(SwitchThread);
    });
    add(r, 0x8058, "set deadline", 1, 0, [](Ctx& c) {
        c.t.deadline = c.vm.now_ms + static_cast<std::int64_t>(c.pop());
        return 0;
    });
    // Pushes whether time remains; the thread resumes (and normally loops back here) once the deadline passes.
    add(r, 0x805A, "wait for deadline", 0, 1, [](Ctx& c) {
        std::int64_t remaining = c.t.deadline - c.vm.now_ms;
        c.push(remaining > 0);
        if (remaining > 0) c.t.wait = {WaitObject::Kind::Time, c.t.deadline};
        return static_cast<int>(StayThread);
    });
    // (wait time, input interrupt flag, input priority) popped from the right. The thread sleeps for `time` x 10 ms
    // (checked against the original: 0x32 sleeps about half a second); the result 0 means the time ran out
    // (1 would mean the input interrupt fired, which a headless run never sees).
    add(r, 0x805C, "wait timing", 3, 1, [](Ctx& c) {
        c.pop(); c.pop(); // priority, flag
        const Word time = c.pop();
        c.push(0);
        std::int64_t until = c.vm.now_ms + static_cast<std::int64_t>(time) * 10;
        if (c.t.objects_until > until) until = c.t.objects_until; // timed display-object changes
        c.t.wait = {WaitObject::Kind::Time, until};
        if (time == 0 && c.t.objects_until <= c.vm.now_ms) c.t.wait = {WaitObject::Kind::Frame, static_cast<std::int64_t>(c.vm.frame) + 1};
        return static_cast<int>(StayThread);
    });
    // 80:4C (thread, a, b, c): tells a thread blocked in a wait object about an event; a = 1 cuts an 80:5C wait short (the wait then
    // yields 1 instead of 0). Pushes 1 when the thread was waiting, 0 otherwise. The other waits (text, selection, tween) are not modelled.
    add(r, 0x804C, "notify waiting thread", 4, 1, [](Ctx& c) {
        c.pop(), c.pop(); // b, c
        const Word a = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        Thread* target = c.vm.find(id);
        Word result = 0;
        if (target && !target->dead && target->wait.kind == WaitObject::Kind::Time && a == 1 && target->depth > 0) {
            target->expr[target->depth - 1] = 1;
            target->wait = {WaitObject::Kind::Frame, static_cast<std::int64_t>(c.vm.frame) + 1};
            result = 1;
        }
        c.push(result);
        return 0;
    });
    add(r, 0x805F, "yield", 0, 0, [](Ctx&) { return static_cast<int>(NextThread); });
    add(r, 0x806A, "yield rest of frame", 0, 0, [](Ctx&) { return static_cast<int>(SkipFrame); });
}

} // namespace obgi::vm
