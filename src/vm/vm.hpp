#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"

// Native (_bp) bytecode VM. Semantics follow analysis/VM分析.md and the verified research prototype
// (scripts/vm_proto.py, scripts/vm_mt.py). Script threads are plain state, not OS threads.
namespace obgi::vm {

using Word = std::uint32_t;

// Addresses are tagged: bits 26..31 select a segment, the low 26 bits are the offset inside it.
enum class Tag : std::uint32_t { Global = 0, Program = 1, Stack = 2, Heap = 3 };
constexpr unsigned kTagShift = 26;
constexpr Word kOffsetMask = (1u << kTagShift) - 1;
// An offset outside the segment (e.g. a negative stack-relative one) must stay out of range, not leak into the tag.
constexpr Word make_addr(Tag tag, Word offset) {
    return (static_cast<Word>(tag) << kTagShift) | (offset > kOffsetMask ? kOffsetMask : offset);
}

// Return codes of a sub-opcode / slice, as consumed by the scheduler (FUN_0049be00).
enum Rc : int { Continue = 0, NextThread = 1, StayThread = 2, SwitchThread = 3, ExitThread = 4, NextThread2 = 5, SkipFrame = 6 };

// Demand-zero 64 MiB segment used for global memory and the heap. The mapping is
// created on first write; physical pages are supplied by the OS on demand.
class PagedSegment {
  public:
    bool read(Word off, std::size_t n, std::uint8_t* out) const;
    bool write(Word off, std::size_t n, const std::uint8_t* in);
    void clear() { data_.reset(); }
    // Sets a byte range back to zero; complete OS pages may be reclaimed.
    void zero(Word off, std::size_t n);

  private:
    static constexpr std::size_t kSize = std::size_t{1} << kTagShift;
    struct Unmap {
        void operator()(std::uint8_t* data) const noexcept;
    };
    std::unique_ptr<std::uint8_t, Unmap> data_;
};

// Script-visible allocations (80:20 / 80:21) inside the heap segment. Addresses are opaque to scripts; blocks are
// 16-byte aligned, first fit, and zeroed when freed so a reused block always reads as zero.
class HeapAllocator {
  public:
    Result<Word> allocate(PagedSegment& heap, Word size);
    // false when `offset` is not the start of a live block
    bool release(PagedSegment& heap, Word offset);
    std::size_t live_blocks() const { return blocks_.size(); }

  private:
    std::map<Word, Word> blocks_; // start offset -> size
};

// Source of program blocks; implementations decode the archive member (e.g. DSC) to plain bytes.
class ProgramProvider {
  public:
    virtual ~ProgramProvider() = default;
    virtual Result<std::vector<std::uint8_t>> load(std::string_view arc, std::string_view name) = 0;
};

struct Module {
    Word start = 0;
    Word size = 0;
    std::string arc, name;
};

struct WaitContext {
    std::int64_t now_ms;
    std::uint64_t frame;
    bool notified;
};

struct WaitUpdate {
    enum class Status { Pending, Complete, SuppressFrame } status = Status::Pending;
    std::vector<Word> values;
    static WaitUpdate pending() { return {}; }
    static WaitUpdate complete(std::vector<Word> values = {}) { return {Status::Complete, std::move(values)}; }
};

// The VM owns result delivery. A service only reports completion, never a Thread* or an expression-stack write.
struct WaitObject {
    enum class Kind { None, Time, Frame, Poll, InputTime, Resource } kind = Kind::None;
    std::int64_t until = 0;
    std::function<Result<WaitUpdate>(const WaitContext&)> poll;
    std::function<void()> cleanup; // release a service operation on completion, cancellation or shutdown
    std::vector<Word> values; // results of a plain Time/Frame wait
    std::uint8_t result_count = 0;
    std::uint16_t opcode = 0;
    Word resume_depth = 0;
    Word input_priority = 0;
    bool interrupt_input = false, notified = false;
};

class WaitInput {
  public:
    virtual ~WaitInput() = default;
    virtual void begin(Word priority) = 0;
    virtual bool triggered(Word priority) = 0;
    virtual void end(Word priority) = 0;
};

struct ThreadConfig {
    Word expr_slots = 0x1000;
    Word program_capacity = 0x800000;
    Word stack_size = 0x400000;
};

struct Thread {
    struct Call {
        Word origin, ret;
    };

    // Ids are never reused, so a stale id can only ever refer to a dead thread.
    Word id = 0;
    bool dead = false;
    ThreadConfig config;
    std::vector<std::uint8_t> program;
    std::vector<Module> modules;
    std::vector<std::uint8_t> stack; // data/call stack bytes (tag Stack)
    std::vector<Word> expr;          // expression stack
    Word depth = 0;                  // current expression stack depth
    Word sp = 0, ip = 0;
    std::vector<Call> calls;
    WaitObject wait;
    std::int64_t deadline = 0;
    std::int64_t objects_until = 0; // virtual time at which the timed display-object changes installed by 90:28 finish (80:5C waits)
    std::deque<Word> queue; // inbox (80:48 / 80:4A)
};

class Vm;

// Execution context handed to the interpreter and to sub-opcode handlers. Failures are sticky:
// after the first one every accessor returns zero and `error` holds the cause.
class Ctx {
  public:
    Ctx(Vm& vm, Thread& t) : vm(vm), t(t) {}

    Word pop();
    void push(Word v);
    std::int64_t read(Word addr, unsigned width, bool sign_extend);
    void write(Word addr, Word value, unsigned width);
    void read_bytes(Word addr, std::size_t n, std::vector<std::uint8_t>& out);
    void write_bytes(Word addr, const std::uint8_t* data, std::size_t n);
    std::string cstring(Word addr, std::size_t max = 4096);
    void write_cstring(Word addr, std::string_view s);
    void suspend(WaitObject wait, std::uint8_t results);

    bool ok() const { return !error.has_value(); }
    void fail(Errc code, std::string message) {
        if (!error) error = Error{code, std::move(message)};
    }

    Vm& vm;
    Thread& t;
    std::optional<Error> error;
};

using SubopFn = std::function<int(Ctx&)>;

struct SubopDef {
    std::uint16_t id = 0; // main opcode << 8 | sub opcode
    std::string name;
    std::uint8_t pops = 0, pushes = 0; // checked against the real stack movement after every call
    SubopFn fn;
    std::uint8_t completion_pushes = 0; // maximum of `pushes` delivered by the installed wait instead of immediately
};

class SubopRegistry {
  public:
    void add(SubopDef def) {
        if (defs_.contains(def.id)) throw std::logic_error("duplicate sub-opcode: " + def.name);
        const auto id = def.id;
        auto [it, inserted] = defs_.emplace(id, std::move(def));
        (void)inserted;
        auto& page = index_[id >> 8];
        if (!page) page = std::make_unique<IndexPage>();
        (*page)[id & 255] = &it->second;
    }
    const SubopDef* find(std::uint16_t id) const {
        const auto& page = index_[id >> 8];
        return page ? (*page)[id & 255] : nullptr;
    }
    std::size_t size() const { return defs_.size(); }
    const std::map<std::uint16_t, SubopDef>& all() const { return defs_; }

  private:
    std::map<std::uint16_t, SubopDef> defs_;
    using IndexPage = std::array<const SubopDef*, 256>;
    std::array<std::unique_ptr<IndexPage>, 256> index_{};
};

// Thread, program and scheduling sub-opcodes (80:40.. 80:6A); everything else lives in bindings.
void register_core_subops(SubopRegistry& registry);

enum class FrameStatus { Ok, AllThreadsDead };

std::string subop_name(std::uint16_t id);

class Vm {
  public:
    Vm(ProgramProvider& provider, const SubopRegistry& registry) : provider(provider), registry(registry) {}
    ~Vm();

    // Creates a thread and loads arc/name as its first program block.
    Result<Thread*> spawn(const ThreadConfig& cfg, std::string_view arc, std::string_view name);
    // Appends a program block to the thread's program area; returns its start offset.
    Result<Word> load_module(Thread& t, std::string_view arc, std::string_view name);
    Result<void> unload_module(Thread& t);

    Thread* find(Word id);
    const std::vector<std::unique_ptr<Thread>>& threads() const { return threads_; }

    // Runs every ready thread once (one scheduler frame) at virtual time `now_ms`.
    Result<FrameStatus> run_frame(std::int64_t now_ms);
    // Resume resource waiters at a scheduler safe point without advancing a game frame.
    Result<FrameStatus> resources_ready(std::int64_t now_ms);

    // Executes one instruction of `t`; returns a scheduler code or an error.
    Result<int> step(Thread& t);
    void cancel_wait(Thread& t);
    void notify_resource_wait() { resource_signal_pending_ = true; }
    // Safe point after a successful scheduler pass; releases dead threads and their service operations.
    void collect_dead();

    PagedSegment global, heap;
    HeapAllocator allocator;
    // Development aid: called after each sub-opcode with the thread, its id and the values it left on the expression
    // stack (top last, at most `pushes` of them).
    std::function<void(const Thread&, std::uint16_t, const std::vector<Word>&)> trace;
    // Development aid: called before every instruction with the thread (ip = the instruction) and its opcode byte.
    std::function<void(const Thread&, std::uint8_t)> trace_op;
    // Called at the start of every scheduler frame with the virtual time (display object motions advance here).
    std::function<void(std::int64_t)> frame_hook;
    std::function<void(const Thread&, std::uint16_t, const std::vector<Word>&)> trace_completion;
    WaitInput* wait_input = nullptr; // must outlive the VM
    Word exclusive_thread = 0;
    std::int64_t now_ms = 0;
    std::uint64_t frame = 0;
    Word switch_target = 0;
    // The real game has no script global memory until the first 80:70: before it reads give zero and writes are lost.
    // Hosts that run the original scripts turn this off; 80:70 turns it on.
    bool global_ready = true;
    std::map<unsigned, std::pair<std::string, std::string>> extensions; // FF F0/F1 registrations
    ProgramProvider& provider;
    const SubopRegistry& registry;

    static constexpr std::size_t kSliceLimit = 0x100000;

  private:
    Result<int> run_slice(Thread& t);
    Result<WaitUpdate> update_wait(Thread& t);
    Result<void> deliver_wait(Thread& t, const WaitUpdate& result);
    Result<FrameStatus> dispatch(bool resources_only);
    bool resource_pass_suppressed_ = false;
    bool resource_signal_pending_ = false;
    bool resource_signal_active_ = false;
    Word next_id_ = 1; // the real game's root thread takes id 0, scripts start at 1
    std::vector<std::unique_ptr<Thread>> threads_;
};

} // namespace obgi::vm
