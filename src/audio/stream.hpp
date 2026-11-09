#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "core/bytes.hpp"
#include "core/error.hpp"

namespace obgi {
// Single producer / single consumer interleaved stereo frames at the mixer's rate.
class PcmStream {
  public:
    explicit PcmStream(std::size_t frames) : samples_((frames ? frames : 1) * 2), capacity_(frames ? frames : 1) {}
    std::size_t writable() const;
    std::size_t readable() const;
    std::size_t write(const std::int16_t* samples, std::size_t frames);
    bool read(std::int16_t& left, std::int16_t& right);
    void finish() { finished_.store(true, std::memory_order_release); }
    bool finished() const { return finished_.load(std::memory_order_acquire); }
    std::uint64_t consumed() const { return consumed_.load(std::memory_order_relaxed); }
  private:
    std::vector<std::int16_t> samples_;
    std::size_t capacity_;
    std::atomic<std::uint64_t> read_{0}, write_{0}, consumed_{0};
    std::atomic<bool> finished_{false};
};

class StreamAudioDecoder {
  public:
    static Result<std::unique_ptr<StreamAudioDecoder>> open(ByteView file, const std::string& name, std::uint32_t output_rate);
    ~StreamAudioDecoder();
    // Returns a block of stereo S16 samples. An empty block marks EOF.
    Result<std::vector<std::int16_t>> next();
    double first_pts() const;
  private:
    StreamAudioDecoder();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
