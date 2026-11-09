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

Vm::~Vm() {
    for (auto& t : threads_) cancel_wait(*t);
}

void Vm::cancel_wait(Thread& t) {
    if (t.wait.cleanup) t.wait.cleanup();
    if (t.wait.kind == WaitObject::Kind::InputTime && t.wait.interrupt_input && wait_input)
        wait_input->end(t.wait.input_priority);
    t.wait = {};
}

void Vm::collect_dead() {
    for (auto& t : threads_)
        if (t->dead) {
            cancel_wait(*t);
            if (exclusive_thread == t->id) exclusive_thread = 0;
        }
    std::erase_if(threads_, [](const auto& t) { return t->dead; });
}

Result<WaitUpdate> Vm::update_wait(Thread& t) {
    auto& w = t.wait;
    switch (w.kind) {
    case WaitObject::Kind::None: return WaitUpdate::complete();
    case WaitObject::Kind::Time:
        return now_ms >= w.until ? WaitUpdate::complete(w.values) : WaitUpdate::pending();
    case WaitObject::Kind::Frame:
        return static_cast<std::int64_t>(frame) >= w.until ? WaitUpdate::complete(w.values) : WaitUpdate::pending();
    case WaitObject::Kind::InputTime:
        if (now_ms >= w.until || w.notified) return WaitUpdate::complete({0});
        if (w.interrupt_input && wait_input && wait_input->triggered(w.input_priority)) return WaitUpdate::complete({1});
        return WaitUpdate::pending();
    case WaitObject::Kind::Poll:
        if (!w.poll) return fail(Errc::Format, "wait has no update function");
        return w.poll({now_ms, frame, w.notified});
    }
    return fail(Errc::Format, "unknown wait kind");
}

Result<FrameStatus> Vm::run_frame(std::int64_t now) {
    now_ms = now;
    if (frame_hook) frame_hook(now);
    bool skip_rest = false;
    std::size_t i = 0;
    while (i < threads_.size()) {
        Thread& t = *threads_[i];
        if (t.dead) {
            ++i;
            continue;
        }
        if (exclusive_thread && t.id != exclusive_thread) { ++i; continue; }
        if (t.wait.kind != WaitObject::Kind::None) {
            auto update = update_wait(t);
            if (!update) {
                update.error().message = "thread " + std::to_string(t.id) + ": " + subop_name(t.wait.opcode) + ": " + update.error().message;
                return std::unexpected(update.error());
            }
            if (update->status == WaitUpdate::Status::Pending) { ++i; continue; }
            if (update->values.size() != t.wait.result_count || t.depth != t.wait.resume_depth)
                return fail(Errc::Format, "completion stack mismatch for " + subop_name(t.wait.opcode));
            const auto opcode = t.wait.opcode;
            Ctx context(*this, t);
            for (Word value : update->values) context.push(value);
            if (trace_completion) trace_completion(t, opcode, update->values);
            if (update->status == WaitUpdate::Status::SuppressFrame) skip_rest = true;
            cancel_wait(t);
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
            if (exclusive_thread == t.id) exclusive_thread = 0;
            ++i;
            break;
        case StayThread:
            // Recheck this thread, including a newly installed wait, in this pass.
            break;
        case SwitchThread: {
            if (switch_target == 0) { // the real game's native root thread: control goes back to its main loop (end of frame)
                skip_rest = true;
                ++i;
                break;
            }
            auto it = std::find_if(threads_.begin(), threads_.end(),
                                   [&](auto& p) { return p->id == switch_target && !p->dead; });
            if (it == threads_.end()) { i = threads_.size(); break; }
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
    collect_dead();
    if (threads_.empty()) return FrameStatus::AllThreadsDead;
    return FrameStatus::Ok;
}

} // namespace obgi::vm
