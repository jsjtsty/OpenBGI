#pragma once
#include <memory>

#include "audio/mixer.hpp"

namespace obgi {

// Opens the default playback device (SDL audio) and feeds it from a Mixer, which must outlive it.
class AudioDevice {
  public:
    static Result<std::unique_ptr<AudioDevice>> open(Mixer& mixer);
    ~AudioDevice();
    AudioDevice(const AudioDevice&) = delete;
    AudioDevice& operator=(const AudioDevice&) = delete;

    const char* driver_name() const;

    struct Impl; // SDL callback state; not for callers

  private:
    AudioDevice() = default;
    std::unique_ptr<Impl> impl_;
};

} // namespace obgi
