#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>

namespace obgi {

// One bounded-concurrency worker, rather than one OS thread per decode. Jobs own their inputs.
// Destroying a future never blocks; shutdown drains queued work before releasing the worker.
class Worker {
  public:
    explicit Worker(bool inline_jobs = false) : inline_(inline_jobs) {
        if (!inline_) thread_ = std::jthread([this] { run(); });
    }
    ~Worker() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_one();
        if (thread_.joinable()) thread_.join();
    }
    template<class F> auto submit(F fn) {
        using T = std::invoke_result_t<F>;
        auto task = std::make_shared<std::packaged_task<T()>>(std::move(fn));
        auto result = task->get_future();
        if (inline_) (*task)();
        else {
            { std::lock_guard lock(mutex_); jobs_.emplace_back([task] { (*task)(); }); }
            ready_.notify_one();
        }
        return result;
    }
  private:
    void run() {
        for (;;) {
            std::function<void()> job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty()) return;
                job = std::move(jobs_.front());
                jobs_.pop_front();
            }
            job();
        }
    }
    bool inline_, stopping_ = false;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> jobs_;
    std::jthread thread_;
};

} // namespace obgi
