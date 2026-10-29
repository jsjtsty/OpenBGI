#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
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

// Sparse zero-filled 64 MiB segment used for global memory and the heap.
class PagedSegment {
  public:
    bool read(Word off, std::size_t n, std::uint8_t* out) const;
    bool write(Word off, std::size_t n, const std::uint8_t* in);
    void clear() { pages_.clear(); }
    // Sets a byte range back to zero (whole pages are released).
    void zero(Word off, std::size_t n);

  private:
    static constexpr std::size_t kPage = 4096;
    using Page = std::array<std::uint8_t, kPage>;
    std::unordered_map<Word, std::unique_ptr<Page>> pages_;
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

struct WaitObject {
    enum class Kind { None, Time, Frame, Poll } kind = Kind::None; // Poll: the thread resumes once `poll` returns true
    std::int64_t until = 0;
    std::function<bool()> poll;
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
};

class SubopRegistry {
  public:
    void add(SubopDef def) { defs_[def.id] = std::move(def); }
    const SubopDef* find(std::uint16_t id) const {
        auto it = defs_.find(id);
        return it == defs_.end() ? nullptr : &it->second;
    }
    std::size_t size() const { return defs_.size(); }

  private:
    std::map<std::uint16_t, SubopDef> defs_;
};

// Thread, program and scheduling sub-opcodes (80:40.. 80:6A); everything else lives in bindings.
void register_core_subops(SubopRegistry& registry);

enum class FrameStatus { Ok, AllThreadsDead };

std::string subop_name(std::uint16_t id);

class Vm {
  public:
    Vm(ProgramProvider& provider, const SubopRegistry& registry) : provider(provider), registry(registry) {}

    // Creates a thread and loads arc/name as its first program block.
    Result<Thread*> spawn(const ThreadConfig& cfg, std::string_view arc, std::string_view name);
    // Appends a program block to the thread's program area; returns its start offset.
    Result<Word> load_module(Thread& t, std::string_view arc, std::string_view name);
    Result<void> unload_module(Thread& t);

    Thread* find(Word id);
    const std::vector<std::unique_ptr<Thread>>& threads() const { return threads_; }

    // Runs every ready thread once (one scheduler frame) at virtual time `now_ms`.
    Result<FrameStatus> run_frame(std::int64_t now_ms);

    // Executes one instruction of `t`; returns a scheduler code or an error.
    Result<int> step(Thread& t);

    PagedSegment global, heap;
    HeapAllocator allocator;
    // Development aid: called after each sub-opcode with the thread, its id and the values it left on the expression
    // stack (top last, at most `pushes` of them).
    std::function<void(const Thread&, std::uint16_t, const std::vector<Word>&)> trace;
    // Development aid: called before every instruction with the thread (ip = the instruction) and its opcode byte.
    std::function<void(const Thread&, std::uint8_t)> trace_op;
    // Called at the start of every scheduler frame with the virtual time (display object motions advance here).
    std::function<void(std::int64_t)> frame_hook;
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
    static constexpr unsigned kSpinLimit = 100000;

  private:
    Result<int> run_slice(Thread& t);
    Word next_id_ = 1; // the real game's root thread takes id 0, scripts start at 1
    std::vector<std::unique_ptr<Thread>> threads_;
};

} // namespace obgi::vm
