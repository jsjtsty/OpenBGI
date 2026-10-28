#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <mutex>

#include "audio/pcm.hpp"

namespace obgi {

// The two channel banks of the original sound system (analysis/音频与子opcode_A0.md): 16 long streams
// (BGM) and 128 short sounds (SE, voice).
enum class Bank : std::uint8_t { Stream = 0, Point = 1 };
constexpr std::uint32_t kStreamChannels = 16;
constexpr std::uint32_t kPointChannels = 128;
constexpr std::uint32_t kMaxVolume = 0x80; // script volumes run 0..0x80
constexpr std::uint32_t kCenterPan = 0x40; // pan runs 0..0x80 around 0x40

enum class ChannelState : std::uint8_t { Empty, Stopped, Playing, Paused };

// Software mixer producing interleaved stereo S16. All control calls may come from any thread; mix() is
// the audio callback. Fades and everything else run on the audio clock (frames mixed), so a given sequence
// of calls and mix() blocks always yields the same samples.
class Mixer {
  public:
    explicit Mixer(std::uint32_t output_rate = 44100) : rate_(output_rate) {}

    static constexpr std::uint32_t channel_count(Bank b) { return b == Bank::Stream ? kStreamChannels : kPointChannels; }
    std::uint32_t output_rate() const { return rate_; }

    // Puts a sound on a channel, stopped. `loop`/`loop_start` come from the file or the script.
    // A pause requested for the channel before loading (pause(…, true)) stays in force.
    Result<void> load(Bank bank, std::uint32_t ch, std::shared_ptr<const Pcm> pcm, bool loop, std::uint32_t loop_start);
    Result<void> unload(Bank bank, std::uint32_t ch);

    // Starts the loaded sound from the beginning at the given volume/pan.
    Result<void> play(Bank bank, std::uint32_t ch, std::uint32_t volume, std::uint32_t pan);
    // Starts the loaded sound from the beginning with the volume and pan it already has.
    Result<void> start(Bank bank, std::uint32_t ch);
    // Pause or resume without losing the position. Pausing an empty channel is remembered for the next load.
    Result<void> set_paused(Bank bank, std::uint32_t ch, bool paused);
    Result<void> stop(Bank bank, std::uint32_t ch);

    Result<void> set_volume(Bank bank, std::uint32_t ch, std::uint32_t volume);
    // Per-channel "configured" volume of the options menu; multiplies with the script volume.
    Result<void> set_config_volume(Bank bank, std::uint32_t ch, std::uint32_t volume);
    Result<void> set_pan(Bank bank, std::uint32_t ch, std::uint32_t pan);
    // Linear volume ramp to `target` over `ms`; with stop_after the channel stops when it gets there.
    Result<void> fade_to(Bank bank, std::uint32_t ch, std::uint32_t target, std::uint32_t ms, bool stop_after);

    ChannelState state(Bank bank, std::uint32_t ch) const;
    // Playback position in source frames (0 for an empty channel).
    std::uint32_t position(Bank bank, std::uint32_t ch) const;

    // Renders `frames` stereo frames, replacing the contents of `out`.
    void mix(std::int16_t* out, std::uint32_t frames);

  private:
    struct Channel {
        std::shared_ptr<const Pcm> pcm;
        bool loop = false;
        std::uint32_t loop_start = 0;
        bool playing = false;
        bool paused = false; // survives load/unload: the script pauses before loading
        double pos = 0;      // source frames
        float volume = 0x80; // current, script scale
        float target = 0x80;
        float step = 0;      // volume change per output frame while fading
        bool stop_after = false;
        std::uint32_t config = kMaxVolume;
        std::uint32_t pan = kCenterPan;
    };

    Channel* find(Bank bank, std::uint32_t ch);
    const Channel* find(Bank bank, std::uint32_t ch) const;
    static Error bad_channel() { return Error{Errc::Range, "sound channel out of range"}; }

    mutable std::mutex mutex_;
    std::uint32_t rate_;
    std::array<Channel, kStreamChannels> streams_{};
    std::array<Channel, kPointChannels> points_{};
};

} // namespace obgi
