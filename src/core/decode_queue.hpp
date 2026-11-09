#pragma once
#include <condition_variable>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace obgi {

// Admission bounds queued/running work. Ready immutable results belong to their consumers/cache.
// Zero worker threads selects a manually pumped executor for deterministic tests.
class DecodeQueue {
    struct Work {
        std::function<void()> run, cancel;
        std::atomic<bool> started{false};
        ~Work() { if (!started.load(std::memory_order_acquire) && cancel) cancel(); }
    };
    struct State {
        std::mutex mutex;
        std::condition_variable wake;
        std::deque<std::function<void()>> jobs;
        std::size_t admitted = 0, bytes = 0;
        bool closing = false;
    };
  public:
    struct Permit {
        std::shared_ptr<State> state;
        std::size_t bytes;
        ~Permit() {
            std::lock_guard lock(state->mutex);
            --state->admitted; state->bytes -= bytes;
        }
    };
    struct Stats { std::size_t queued, admitted, bytes; };
    explicit DecodeQueue(unsigned workers = 2, std::size_t limit = 8, std::size_t bytes = 256u * 1024u * 1024u)
        : state_(std::make_shared<State>()), limit_(limit), byte_limit_(bytes) {
        for (unsigned i = 0; i < workers; ++i) workers_.emplace_back([state = state_] {
            for (;;) {
                std::function<void()> job;
                {
                    std::unique_lock lock(state->mutex);
                    state->wake.wait(lock, [&] { return state->closing || !state->jobs.empty(); });
                    if (state->closing) return;
                    job = std::move(state->jobs.front()); state->jobs.pop_front();
                }
                job();
            }
        });
    }
    ~DecodeQueue() {
        std::deque<std::function<void()>> discarded;
        {
            std::lock_guard lock(state_->mutex);
            state_->closing = true;
            discarded.swap(state_->jobs);
        }
        // Destroy queued closures outside the mutex: their permits return admission.
        discarded.clear();
        state_->wake.notify_all();
        workers_.clear(); // only jobs already running may finish; no queued work is drained
    }
    std::shared_ptr<Permit> try_submit(std::size_t bytes, std::function<void()> job, std::function<void()> cancel = {}) {
        std::shared_ptr<Permit> permit;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->closing || state_->admitted >= limit_) return {};
            // A single large legal asset may run alone; the budget never rejects its format.
            if (state_->admitted && (bytes > byte_limit_ || state_->bytes > byte_limit_ - bytes)) return {};
            permit = std::shared_ptr<Permit>(new Permit{state_, bytes});
            ++state_->admitted; state_->bytes += bytes;
            auto work = std::make_shared<Work>(); work->run = std::move(job); work->cancel = std::move(cancel);
            state_->jobs.push_back([permit, work] { work->started.store(true, std::memory_order_release); work->run(); });
        }
        state_->wake.notify_one();
        return permit;
    }
    bool run_one() {
        if (!workers_.empty()) return false; // manual mode only; never steal decoding onto the UI thread
        std::function<void()> job;
        {
            std::lock_guard lock(state_->mutex);
            if (state_->jobs.empty()) return false;
            job = std::move(state_->jobs.front()); state_->jobs.pop_front();
        }
        job();
        return true;
    }
    Stats stats() const {
        std::lock_guard lock(state_->mutex);
        return {state_->jobs.size(), state_->admitted, state_->bytes};
    }
  private:
    std::shared_ptr<State> state_;
    std::vector<std::jthread> workers_;
    std::size_t limit_, byte_limit_;
};
} // namespace obgi
