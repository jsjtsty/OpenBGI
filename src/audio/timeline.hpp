#pragma once
#include <algorithm>
#include <array>
#include "audio/mixer.hpp"

namespace obgi {

// Silent playback still consumes samples. Keep the fractional millisecond remainder so
// changing scheduler frequency does not change positions, loops or fades.
class AudioTimeline {
  public:
    explicit AudioTimeline(Mixer& mixer) : mixer_(mixer) {}
    void advance(std::int64_t now_ms) {
        const auto elapsed = std::max<std::int64_t>(0, now_ms - last_ms_);
        last_ms_ = now_ms;
        const auto scaled = std::uint64_t(elapsed) * mixer_.output_rate() + remainder_;
        auto frames = scaled / 1000;
        remainder_ = scaled % 1000;
        std::array<std::int16_t, 1024> scratch{};
        while (frames) {
            const auto count = static_cast<std::uint32_t>(std::min<std::uint64_t>(frames, 512));
            mixer_.mix(scratch.data(), count);
            frames -= count;
        }
    }
  private:
    Mixer& mixer_;
    std::int64_t last_ms_ = 0;
    std::uint64_t remainder_ = 0;
};

} // namespace obgi
