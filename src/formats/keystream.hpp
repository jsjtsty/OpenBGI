#pragma once
#include <cstdint>

namespace obgi {

// The LCG keystream shared by DSC, SDC and CompressedBG: next() returns bits 16..30 of
// state*0x015a4e35 (before the +1 step), then advances state = product + 1.
class Keystream {
  public:
    explicit Keystream(std::uint32_t seed) : state_(seed) {}
    std::uint32_t next() {
        std::uint32_t product = state_ * 0x015a4e35u;
        state_ = product + 1;
        return (product >> 16) & 0x7fff;
    }

  private:
    std::uint32_t state_;
};

} // namespace obgi
