#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <future>
#include <fstream>
#include <list>
#include <map>
#include <optional>
#include "core/decode_queue.hpp"
#include "resources/archive_store.hpp"
#include "formats/dsc.hpp"

namespace obgi {

struct ResourceDecodeTiming { std::int64_t dsc_us = 0; };
inline thread_local ResourceDecodeTiming* active_resource_timing = nullptr;
struct ResourceTimingScope {
    ResourceDecodeTiming* previous;
    explicit ResourceTimingScope(ResourceDecodeTiming* current) : previous(active_resource_timing) { active_resource_timing = current; }
    ~ResourceTimingScope() { active_resource_timing = previous; }
};

// Finder, LRU and poll/submit are owned by the VM thread. Workers see only owned
// bytes and a pure decoder, never this cache, a finder, a VM or a GPU object.
template<class T> class DecodedCache {
  public:
    using Pointer = std::shared_ptr<const T>;
    using Decoder = std::function<Result<Pointer>(OwnedBytes)>;
    using Size = std::function<std::size_t(const T&)>;
  private:
    using Key = std::pair<std::string, std::string>;
    struct Task {
        std::promise<Result<Pointer>> promise;
        std::shared_future<Result<Pointer>> future = promise.get_future().share();
        std::function<Result<Pointer>()> decode;
        std::atomic<bool> cancelled{false};
        unsigned users = 0; // request ownership is managed only on the VM thread
        bool submitted = false, cacheable = false, cached = false;
        std::uint64_t epoch = 0;
        Key key;
        std::size_t estimate = 0;
        std::chrono::steady_clock::time_point requested = std::chrono::steady_clock::now();
    };
  public:
    struct Ticket {
        std::shared_ptr<Task> task;
        explicit Ticket(std::shared_ptr<Task> value) : task(std::move(value)) { ++task->users; }
        ~Ticket() { if (--task->users == 0) task->cancelled.store(true, std::memory_order_release); }
    };
    using Request = std::shared_ptr<Ticket>;
    DecodedCache(ArcResourceFinder& finder, Decoder decoder, Size size, std::size_t limit, DecodeQueue* queue = nullptr)
        : finder_(finder), decoder_(std::move(decoder)), size_(std::move(size)), limit_(limit), queue_(queue) {}
    void set_queue(DecodeQueue* queue) { queue_ = queue; }
    std::size_t bytes() const { return bytes_; }
    std::size_t count() const { return cache_.size(); }
    void clear() { cache_.clear(); lru_.clear(); bytes_ = 0; ++epoch_; pending_.clear(); }
    Result<Pointer> load_sync(std::string_view prefix, std::string_view name) {
        auto request = prepare(prefix, name, false);
        if (!request) return std::unexpected(request.error());
        return *poll(*request);
    }
    Result<Request> request(std::string_view prefix, std::string_view name) { return prepare(prefix, name, queue_ != nullptr); }
    Request transform(std::string label, std::function<Result<Pointer>()> decode, std::size_t estimate) {
        auto task = std::make_shared<Task>();
        task->key = {"transform", std::move(label)}; task->decode = std::move(decode); task->estimate = estimate;
        auto request = std::make_shared<Ticket>(task);
        if (!queue_) { task->submitted = true; execute(task); }
        else (void)poll(request);
        return request;
    }
    std::optional<Result<Pointer>> poll(const Request& request) {
        auto task = request->task;
        if (!task->submitted) {
            auto permit = queue_->try_submit(task->estimate, [task] { execute(task); }, [task] {
                task->cancelled.store(true, std::memory_order_release);
                task->decode = {};
                task->promise.set_value(fail(Errc::Io, "resource queue stopped"));
            });
            if (!permit) return std::nullopt;
            // The queue closure holds admission until decode finishes. A ready result
            // must not block other VM threads from decoding their dependencies.
            task->submitted = true;
        }
        if (task->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return std::nullopt;
        try {
            auto result = task->future.get();
            if (result && task->cacheable && !task->cached && task->epoch == epoch_) {
                task->cached = true;
                insert(task->key, *result);
            }
            return result;
        } catch (const std::exception& e) {
            return fail(Errc::Io, "resource job failed: " + std::string(e.what()));
        }
    }
  private:
    static bool trace() { static const bool enabled = std::getenv("OBGI_PERF_RESOURCE") != nullptr; return enabled; }
    static void execute(const std::shared_ptr<Task>& task) {
        try {
            if (task->cancelled.load(std::memory_order_acquire)) {
                task->decode = {};
                task->promise.set_value(fail(Errc::Io, "resource request cancelled"));
                return;
            }
            const auto start = std::chrono::steady_clock::now();
            ResourceDecodeTiming timing;
            ResourceTimingScope scope(trace() ? &timing : nullptr);
            auto decode = std::move(task->decode); // release input ownership after the decoder finishes
            auto result = decode();
            const auto total_us = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
            if (trace()) std::fprintf(stderr, "RESOURCE decode [%s/%s] us=%lld dsc_us=%lld codec_us=%lld queue_us=%lld ok=%d\n", task->key.first.c_str(), task->key.second.c_str(),
                static_cast<long long>(total_us), static_cast<long long>(timing.dsc_us), static_cast<long long>(total_us - timing.dsc_us),
                static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(start - task->requested).count()), bool(result));
            task->promise.set_value(std::move(result));
        } catch (const std::exception& e) {
            task->promise.set_value(fail(Errc::Io, "resource decode failed: " + std::string(e.what())));
        } catch (...) {
            task->promise.set_value(fail(Errc::Io, "resource decode failed"));
        }
    }
    Result<Request> prepare(std::string_view prefix, std::string_view name, bool async) {
        const auto start = std::chrono::steady_clock::now();
        auto loose_path = finder_.find_loose_file(name);
        const bool loose = loose_path.has_value();
        Key key{ascii_lower(prefix), ascii_lower(name)};
        if (!loose) {
            if (auto it = cache_.find(key); it != cache_.end()) {
                lru_.splice(lru_.begin(), lru_, it->second.lru);
                auto task = std::make_shared<Task>();
                task->submitted = true; task->promise.set_value(it->second.value);
                if (trace()) std::fprintf(stderr, "RESOURCE hit [%s/%s]\n", key.first.c_str(), key.second.c_str());
                return std::make_shared<Ticket>(std::move(task));
            }
            if (async) if (auto it = pending_.find(key); it != pending_.end())
                if (auto task = it->second.lock(); task && !task->cancelled.load(std::memory_order_acquire))
                    return std::make_shared<Ticket>(std::move(task));
        }
        bool from_loose = loose;
        OwnedBytes raw;
        if (!loose) {
            auto opened = finder_.open_raw(prefix, name, &from_loose);
            if (!opened) return std::unexpected(opened.error());
            raw = std::move(*opened);
            if (from_loose) {
                // An override appeared between the first lookup and resolution.
                loose_path = finder_.find_loose_file(name);
                if (!loose_path) return fail(Errc::Io, "loose resource changed during resolution");
                raw = {};
            }
        }
        if (trace()) std::fprintf(stderr, "RESOURCE resolve [%s/%s] us=%lld bytes=%zu loose=%d\n", key.first.c_str(), key.second.c_str(),
            static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count()), raw.bytes.size(), from_loose);
        auto task = std::make_shared<Task>();
        task->key = key; task->cacheable = !loose && !from_loose; task->epoch = epoch_;
        // Conservative admission hint, not a claim of an allocator-enforced memory ceiling.
        task->estimate = raw.bytes.size() + 64u * 1024u * 1024u;
        auto decoder = decoder_;
        task->decode = [decoder = std::move(decoder), raw = std::move(raw), loose_path]() -> Result<Pointer> {
            if (loose_path) {
                // Mutable loose files are copied from a file handle on the worker.
                // A concurrent truncation must report a read/decode error, not fault an mmap.
                std::ifstream file(*loose_path, std::ios::binary | std::ios::ate);
                if (!file) return fail(Errc::Io, "cannot open loose resource: " + loose_path->string());
                const auto length = file.tellg();
                if (length < 0) return fail(Errc::Io, "cannot size loose resource: " + loose_path->string());
                auto bytes = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(length));
                file.seekg(0);
                if (!file.read(reinterpret_cast<char*>(bytes->data()), static_cast<std::streamsize>(bytes->size())))
                    return fail(Errc::Io, "cannot read loose resource: " + loose_path->string());
                return decoder(OwnedBytes{bytes, ByteView(*bytes)});
            }
            return decoder(raw);
        };
        auto request = std::make_shared<Ticket>(task);
        if (!async) { task->submitted = true; execute(task); }
        else {
            if (pending_.size() >= 256) std::erase_if(pending_, [](const auto& item) {
                auto task = item.second.lock();
                return !task || task->cancelled.load(std::memory_order_acquire);
            });
            if (task->cacheable) pending_[key] = task;
            // Try immediately; a full queue stays pending and retries from Poll, never decodes inline.
            (void)poll(request);
        }
        return request;
    }
    void insert(const Key& key, Pointer value) {
        const auto bytes = size_(*value);
        if (!bytes || bytes > limit_) return;
        if (auto it = cache_.find(key); it != cache_.end()) {
            bytes_ -= size_(*it->second.value); lru_.erase(it->second.lru); cache_.erase(it);
        }
        while (bytes_ > limit_ - bytes) {
            auto it = cache_.find(lru_.back());
            bytes_ -= size_(*it->second.value); cache_.erase(it); lru_.pop_back();
        }
        lru_.push_front(key); cache_.emplace(key, Entry{std::move(value), lru_.begin()}); bytes_ += bytes;
    }
    struct Entry { Pointer value; typename std::list<Key>::iterator lru; };
    ArcResourceFinder& finder_;
    Decoder decoder_;
    Size size_;
    std::size_t limit_, bytes_ = 0;
    DecodeQueue* queue_;
    std::uint64_t epoch_ = 0;
    std::map<Key, Entry> cache_;
    std::list<Key> lru_;
    std::map<Key, std::weak_ptr<Task>> pending_;
};

inline Result<OwnedBytes> unpack_resource(OwnedBytes raw) {
    if (!is_dsc(raw.bytes)) return raw;
    const auto start = active_resource_timing ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    auto decoded = dsc_decode(raw.bytes);
    if (active_resource_timing) active_resource_timing->dsc_us += std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start).count();
    if (!decoded) return std::unexpected(decoded.error());
    auto owner = std::make_shared<std::vector<std::uint8_t>>(std::move(*decoded));
    return OwnedBytes{owner, ByteView(*owner)};
}
} // namespace obgi
