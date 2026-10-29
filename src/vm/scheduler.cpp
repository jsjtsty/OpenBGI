#include <algorithm>
#include <cstdio>

#include "vm/vm.hpp"

namespace obgi::vm {

std::string subop_name(std::uint16_t id) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X:%02X", id >> 8, id & 0xff);
    return buf;
}

Result<Word> Vm::load_module(Thread& t, std::string_view arc, std::string_view name) {
    auto block = provider.load(arc, name);
    if (!block) return std::unexpected(block.error());
    // A _bp block starts with {code offset, code length}; only the code is copied into the program area.
    if (block->size() < 8) return fail(Errc::Format, "program block too short: " + std::string(name));
    Word off = load_u32le(block->data()), n = load_u32le(block->data() + 4);
    if (static_cast<std::uint64_t>(off) + n > block->size())
        return fail(Errc::Format, "truncated program block: " + std::string(name));
    Word start = static_cast<Word>(t.program.size());
    if (static_cast<std::uint64_t>(start) + n > t.config.program_capacity)
        return fail(Errc::Range, "thread program capacity exceeded loading " + std::string(name));
    t.program.insert(t.program.end(), block->begin() + off, block->begin() + off + n);
    t.modules.push_back({start, n, std::string(arc), std::string(name)});
    return start;
}

Result<void> Vm::unload_module(Thread& t) {
    if (t.modules.empty()) return fail(Errc::Range, "no program block to unload");
    t.program.resize(t.modules.back().start);
    t.modules.pop_back();
    return {};
}

Result<Thread*> Vm::spawn(const ThreadConfig& cfg, std::string_view arc, std::string_view name) {
    auto t = std::make_unique<Thread>();
    t->id = next_id_;
    t->config = cfg;
    t->stack.assign(cfg.stack_size, 0);
    t->expr.assign(std::max<Word>(cfg.expr_slots, 1), 0);
    auto start = load_module(*t, arc, name);
    if (!start) return std::unexpected(start.error());
    t->ip = *start;
    ++next_id_;
    threads_.push_back(std::move(t));
    return threads_.back().get();
}

Thread* Vm::find(Word id) {
    for (auto& t : threads_)
        if (t->id == id && !t->dead) return t.get();
    return nullptr;
}

Result<int> Vm::run_slice(Thread& t) {
    for (std::size_t n = 0; n < kSliceLimit; ++n) {
        auto rc = step(t);
        if (!rc) return rc;
        if (*rc != Continue) return rc;
    }
    return static_cast<int>(Continue); // slice budget used up; the thread keeps its place for next frame
}

static bool ready(const Thread& t, const Vm& vm) {
    switch (t.wait.kind) {
    case WaitObject::Kind::None: return true;
    case WaitObject::Kind::Time: return vm.now_ms >= t.wait.until;
    case WaitObject::Kind::Frame: return static_cast<std::int64_t>(vm.frame) >= t.wait.until;
    case WaitObject::Kind::Poll: return !t.wait.poll || t.wait.poll();
    }
    return true;
}

Result<FrameStatus> Vm::run_frame(std::int64_t now) {
    now_ms = now;
    if (frame_hook) frame_hook(now);
    bool skip_rest = false;
    unsigned spins = 0;
    std::size_t i = 0;
    while (i < threads_.size()) {
        Thread& t = *threads_[i];
        if (t.dead) {
            ++i;
            continue;
        }
        if (t.wait.kind != WaitObject::Kind::None) {
            if (!ready(t, *this)) {
                ++i;
                continue;
            }
            t.wait = {};
        }
        if (skip_rest) {
            ++i;
            continue;
        }
        auto rc = run_slice(t);
        if (!rc) {
            rc.error().message = "thread " + std::to_string(t.id) + ": " + rc.error().message;
            return std::unexpected(rc.error());
        }
        switch (*rc) {
        case ExitThread:
            t.dead = true;
            ++i;
            break;
        case StayThread:
            if (++spins > kSpinLimit)
                return fail(Errc::Format, "thread " + std::to_string(t.id) + " spins on rc=2 without a wait object");
            break;
        case SwitchThread: {
            if (switch_target == 0) { // the real game's native root thread: control goes back to its main loop (end of frame)
                skip_rest = true;
                ++i;
                break;
            }
            auto it = std::find_if(threads_.begin(), threads_.end(),
                                   [&](auto& p) { return p->id == switch_target && !p->dead; });
            if (it == threads_.end()) return fail(Errc::Range, "switch to missing thread");
            i = static_cast<std::size_t>(it - threads_.begin());
            break;
        }
        case SkipFrame:
            skip_rest = true;
            ++i;
            break;
        default: ++i; break;
        }
    }
    ++frame;
    bool alive = std::any_of(threads_.begin(), threads_.end(), [](auto& p) { return !p->dead; });
    if (!alive) return FrameStatus::AllThreadsDead;
    return FrameStatus::Ok;
}

} // namespace obgi::vm
