#include "audio/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace obgi {

Mixer::Channel* Mixer::find(Bank bank, std::uint32_t ch) {
    if (ch >= channel_count(bank)) return nullptr;
    return bank == Bank::Stream ? &streams_[ch] : &points_[ch];
}
const Mixer::Channel* Mixer::find(Bank bank, std::uint32_t ch) const {
    return const_cast<Mixer*>(this)->find(bank, ch);
}

Result<void> Mixer::load(Bank bank, std::uint32_t ch, std::shared_ptr<const Pcm> pcm, bool loop, std::uint32_t loop_start) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    if (!pcm || pcm->frames() == 0) return fail(Errc::Format, "empty sound");
    const bool keep_paused = c->paused;
    *c = Channel{};
    c->paused = keep_paused;
    c->loop = loop;
    c->loop_start = std::min(loop_start, pcm->frames() - 1);
    c->pcm = std::move(pcm);
    return {};
}

Result<void> Mixer::unload(Bank bank, std::uint32_t ch) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    *c = Channel{};
    return {};
}

Result<void> Mixer::play(Bank bank, std::uint32_t ch, std::uint32_t volume, std::uint32_t pan) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    if (!c->pcm) return fail(Errc::Range, "no sound loaded on the channel");
    c->playing = true;
    c->pos = 0;
    c->loops = 0;
    c->volume = c->target = static_cast<float>(std::min(volume, kMaxVolume));
    c->step = 0;
    c->stop_after = false;
    c->pan = std::min<std::uint32_t>(pan, 2 * kCenterPan);
    return {};
}

Result<void> Mixer::start(Bank bank, std::uint32_t ch) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    if (!c->pcm) return fail(Errc::Range, "no sound loaded on the channel");
    c->playing = true;
    c->pos = 0;
    c->loops = 0;
    c->step = 0;
    c->stop_after = false;
    return {};
}

Result<void> Mixer::set_paused(Bank bank, std::uint32_t ch, bool paused) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    c->paused = paused;
    return {};
}

Result<void> Mixer::stop(Bank bank, std::uint32_t ch) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    c->playing = false;
    c->pos = 0;
    return {};
}

Result<void> Mixer::set_volume(Bank bank, std::uint32_t ch, std::uint32_t volume) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    c->volume = c->target = static_cast<float>(std::min(volume, kMaxVolume));
    c->step = 0;
    return {};
}

Result<void> Mixer::set_config_volume(Bank bank, std::uint32_t ch, std::uint32_t volume) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    c->config = std::min(volume, kMaxVolume);
    return {};
}

Result<void> Mixer::set_pan(Bank bank, std::uint32_t ch, std::uint32_t pan) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    c->pan = std::min<std::uint32_t>(pan, 2 * kCenterPan);
    return {};
}

Result<void> Mixer::fade_to(Bank bank, std::uint32_t ch, std::uint32_t target, std::uint32_t ms, bool stop_after) {
    std::lock_guard lock(mutex_);
    Channel* c = find(bank, ch);
    if (!c) return std::unexpected(bad_channel());
    c->target = static_cast<float>(std::min(target, kMaxVolume));
    c->stop_after = stop_after;
    const double frames = double(ms) * rate_ / 1000.0;
    if (frames < 1.0) {
        c->volume = c->target;
        c->step = 0;
        if (stop_after) c->playing = false;
    } else {
        c->step = (c->target - c->volume) / static_cast<float>(frames);
    }
    return {};
}

ChannelState Mixer::state(Bank bank, std::uint32_t ch) const {
    std::lock_guard lock(mutex_);
    const Channel* c = find(bank, ch);
    if (!c || !c->pcm) return ChannelState::Empty;
    if (!c->playing) return ChannelState::Stopped;
    return c->paused ? ChannelState::Paused : ChannelState::Playing;
}

std::uint32_t Mixer::position(Bank bank, std::uint32_t ch) const {
    std::lock_guard lock(mutex_);
    const Channel* c = find(bank, ch);
    return c && c->pcm ? static_cast<std::uint32_t>(c->pos) : 0;
}

std::uint32_t Mixer::loop_count(Bank bank, std::uint32_t ch) const {
    std::lock_guard lock(mutex_);
    const Channel* c = find(bank, ch);
    return c ? c->loops : 0;
}

void Mixer::mix(std::int16_t* out, std::uint32_t frames) {
    std::lock_guard lock(mutex_);
    std::vector<float> acc(std::size_t{frames} * 2, 0.0f);

    auto render = [&](Channel& c) {
        if (!c.playing || c.paused || !c.pcm) return;
        const Pcm& p = *c.pcm;
        const std::uint32_t total = p.frames();
        const double ratio = double(p.sample_rate) / rate_;
        const float pan = (float(c.pan) - float(kCenterPan)) / float(kCenterPan); // -1 .. 1
        const float gl = std::min(1.0f, 1.0f - pan), gr = std::min(1.0f, 1.0f + pan);
        const float cfg = float(c.config) / float(kMaxVolume);
        for (std::uint32_t i = 0; i < frames; ++i) {
            if (c.step != 0) {
                c.volume += c.step;
                if ((c.step > 0 && c.volume >= c.target) || (c.step < 0 && c.volume <= c.target)) {
                    c.volume = c.target;
                    c.step = 0;
                    if (c.stop_after) c.playing = false;
                }
            }
            if (!c.playing) return;
            if (c.pos >= total) {
                if (!c.loop) {
                    c.playing = false;
                    return;
                }
                c.pos = c.loop_start + (c.pos - total);
                if (c.pos >= total) c.pos = c.loop_start;
                ++c.loops;
            }
            const std::uint32_t i0 = static_cast<std::uint32_t>(c.pos);
            const float frac = static_cast<float>(c.pos - i0);
            // at the very end of a looping sound the next sample is the loop start
            const std::uint32_t i1 = i0 + 1 < total ? i0 + 1 : (c.loop ? c.loop_start : i0);
            float l, r;
            if (p.channels == 1) {
                l = r = (p.samples[i0] + (p.samples[i1] - p.samples[i0]) * frac) / 32768.0f;
            } else {
                l = (p.samples[2 * i0] + (p.samples[2 * i1] - p.samples[2 * i0]) * frac) / 32768.0f;
                r = (p.samples[2 * i0 + 1] + (p.samples[2 * i1 + 1] - p.samples[2 * i0 + 1]) * frac) / 32768.0f;
            }
            const float g = c.volume / float(kMaxVolume) * cfg;
            acc[2 * i] += l * g * gl;
            acc[2 * i + 1] += r * g * gr;
            c.pos += ratio;
        }
    };
    for (Channel& c : streams_) render(c);
    for (Channel& c : points_) render(c);

    for (std::size_t i = 0; i < acc.size(); ++i)
        out[i] = static_cast<std::int16_t>(std::lrint(std::clamp(acc[i], -1.0f, 1.0f) * 32767.0f));
}

} // namespace obgi
