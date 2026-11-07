#include "audio/device.hpp"

#include <SDL3/SDL.h>

#include <string>
#include <array>
#include <algorithm>

namespace obgi {

struct AudioDevice::Impl {
    Mixer* mixer = nullptr;
    SDL_AudioStream* stream = nullptr;
    bool inited = false;
    std::array<std::int16_t, 2048> scratch{}; // only touched by the audio callback

    ~Impl() {
        if (stream) SDL_DestroyAudioStream(stream); // closes the device; the callback is finished before it returns
        if (inited) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
};

namespace {

// Called on SDL's audio thread whenever the device wants more data.
void SDLCALL feed(void* user, SDL_AudioStream* stream, int additional, int /*total*/) {
    auto& m = *static_cast<AudioDevice::Impl*>(user);
    std::uint32_t frames = additional > 0 ? static_cast<std::uint32_t>(additional) / 4 : 0;
    while (frames) {
        const auto count = std::min(frames, 1024u);
        m.mixer->mix(m.scratch.data(), count);
        if (!SDL_PutAudioStreamData(stream, m.scratch.data(), static_cast<int>(count * 4))) break;
        frames -= count;
    }
}

} // namespace

AudioDevice::~AudioDevice() = default;

const char* AudioDevice::driver_name() const { return SDL_GetCurrentAudioDriver(); }

Result<std::unique_ptr<AudioDevice>> AudioDevice::open(Mixer& mixer) {
    std::unique_ptr<AudioDevice> d(new AudioDevice);
    d->impl_ = std::make_unique<Impl>();
    Impl& m = *d->impl_;
    m.mixer = &mixer;
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) return fail(Errc::Io, std::string("SDL audio: ") + SDL_GetError());
    m.inited = true;

    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 2;
    spec.freq = static_cast<int>(mixer.output_rate());
    m.stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, &m);
    if (!m.stream) return fail(Errc::Io, std::string("SDL_OpenAudioDeviceStream: ") + SDL_GetError());
    SDL_ResumeAudioStreamDevice(m.stream);
    return d;
}

} // namespace obgi
