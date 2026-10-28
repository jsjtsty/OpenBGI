#include <doctest/doctest.h>

#include <cmath>
#include <cstdlib>

#include <SDL3/SDL.h>

#include "audio/device.hpp"
#include "audio/mixer.hpp"
#include "bindings/audio_subops.hpp"
#include "formats/bw.hpp"
#include "support.hpp"
#include "vm/vm.hpp"

using namespace obgi;

namespace {

std::shared_ptr<Pcm> tone(std::uint32_t frames, std::int16_t level, std::uint32_t channels = 2, std::uint32_t rate = 1000) {
    auto p = std::make_shared<Pcm>();
    p->sample_rate = rate;
    p->channels = channels;
    p->samples.assign(std::size_t{frames} * channels, level);
    return p;
}

std::vector<std::int16_t> render(Mixer& m, std::uint32_t frames) {
    std::vector<std::int16_t> out(std::size_t{frames} * 2);
    m.mix(out.data(), frames);
    return out;
}

} // namespace

TEST_CASE("FFmpeg is the LGPL build") {
    const std::string license = ffmpeg_license();
    MESSAGE("FFmpeg licence: ", license);
    CHECK(license.find("LGPL") != std::string::npos);
    CHECK(license.find("GPL version") != 0); // not the plain GPL string
    CHECK(license.find("nonfree") == std::string::npos);
}

TEST_CASE("mixer plays, loops, pans and fades on the audio clock") {
    Mixer m(1000); // 1 kHz output: 1 ms = 1 frame
    CHECK(m.state(Bank::Stream, 0) == ChannelState::Empty);
    CHECK_FALSE(m.play(Bank::Stream, 0, 0x80, 0x40).has_value()); // nothing loaded
    CHECK_FALSE(m.load(Bank::Stream, 16, tone(10, 1000), false, 0).has_value()); // channel out of range

    // one-shot at full volume
    REQUIRE(m.load(Bank::Stream, 0, tone(10, 16384), false, 0).has_value());
    CHECK(m.state(Bank::Stream, 0) == ChannelState::Stopped);
    REQUIRE(m.play(Bank::Stream, 0, 0x80, 0x40).has_value());
    auto out = render(m, 20);
    CHECK(out[0] == out[1]);
    CHECK(std::abs(out[0] - 16383) <= 1);
    CHECK(out[2 * 9] != 0);   // frame 9 still sounds
    CHECK(out[2 * 12] == 0);  // finished
    CHECK(m.state(Bank::Stream, 0) == ChannelState::Stopped);

    // looping with a loop start keeps playing and never goes silent
    REQUIRE(m.load(Bank::Point, 5, tone(8, 8000, 1), true, 4).has_value());
    REQUIRE(m.play(Bank::Point, 5, 0x40, 0x40).has_value()); // half volume
    out = render(m, 100);
    CHECK(m.state(Bank::Point, 5) == ChannelState::Playing);
    CHECK(std::abs(out[2 * 99] - 4000) <= 1);
    CHECK(m.position(Bank::Point, 5) <= 8); // wraps lazily on the next sample

    // hard pan left: the right channel is silent
    REQUIRE(m.set_pan(Bank::Point, 5, 0).has_value());
    out = render(m, 4);
    CHECK(out[0] != 0);
    CHECK(out[1] == 0);
    REQUIRE(m.stop(Bank::Point, 5).has_value());

    // a 50 ms fade from full to zero, then stop
    REQUIRE(m.load(Bank::Stream, 1, tone(1000, 20000), false, 0).has_value());
    REQUIRE(m.play(Bank::Stream, 1, 0x80, 0x40).has_value());
    REQUIRE(m.fade_to(Bank::Stream, 1, 0, 50, true).has_value());
    out = render(m, 25);
    CHECK(std::abs(out[2 * 24]) < 20000 * 0.55);
    CHECK(std::abs(out[2 * 24]) > 20000 * 0.40);
    render(m, 40);
    CHECK(m.state(Bank::Stream, 1) == ChannelState::Stopped);

    // pause keeps the position; pausing before loading is remembered
    REQUIRE(m.set_paused(Bank::Stream, 2, true).has_value());
    REQUIRE(m.load(Bank::Stream, 2, tone(100, 1000), false, 0).has_value());
    REQUIRE(m.start(Bank::Stream, 2).has_value());
    CHECK(m.state(Bank::Stream, 2) == ChannelState::Paused);
    out = render(m, 10);
    CHECK(out[0] == 0);
    REQUIRE(m.set_paused(Bank::Stream, 2, false).has_value());
    out = render(m, 10);
    CHECK(out[0] != 0);
    CHECK(m.position(Bank::Stream, 2) == 10);
}

TEST_CASE("mixer resamples to the output rate") {
    Mixer m(44100);
    REQUIRE(m.load(Bank::Stream, 0, tone(2205, 10000, 1, 22050), false, 0).has_value()); // 0.1 s at 22.05 kHz
    REQUIRE(m.play(Bank::Stream, 0, 0x80, 0x40).has_value());
    render(m, 4000);
    CHECK(m.state(Bank::Stream, 0) == ChannelState::Playing);
    render(m, 1000); // 0.1 s = 4410 output frames in total
    CHECK(m.state(Bank::Stream, 0) == ChannelState::Stopped);
}

TEST_CASE("real bw sound files decode to the length in their header") {
    if (!std::filesystem::exists(obgi_test::game_dir() / "data05500.arc")) return;
    auto arc = Arc::open(obgi_test::game_dir() / "data05500.arc");
    REQUIRE(arc.has_value());
    std::size_t checked = 0;
    for (const ArcEntry& e : arc->entries()) {
        ByteView data = arc->data(e);
        auto header = bw_parse(data);
        REQUIRE(header.has_value());
        auto pcm = decode_bw(data);
        REQUIRE_MESSAGE(pcm.has_value(), e.name, ": ", std::string(pcm ? "" : pcm.error().message));
        CHECK_MESSAGE((*pcm)->frames() == header->sample_count, e.name);
        CHECK((*pcm)->channels == header->channels);
        CHECK((*pcm)->sample_rate == header->sample_rate);
        if (++checked == 6) break; // BGM files are long; a few are enough
    }
    CHECK(checked == 6);
}

namespace {

struct Asm {
    std::vector<std::uint8_t> b;
    Asm& raw(std::initializer_list<int> bytes) {
        for (int x : bytes) b.push_back(static_cast<std::uint8_t>(x));
        return *this;
    }
    Asm& i32(vm::Word v) { return raw({0x02, int(v & 255), int((v >> 8) & 255), int((v >> 16) & 255), int(v >> 24)}); }
    Asm& sub(int op, int sub) { return raw({op, sub}); }
    Asm& ret() { return raw({0x17}); }
};

class OneProgram final : public vm::ProgramProvider {
  public:
    std::vector<std::uint8_t> program;
    void set(const Asm& a) {
        program = {8, 0, 0, 0};
        for (int i = 0; i < 4; ++i) program.push_back(static_cast<std::uint8_t>(a.b.size() >> (8 * i)));
        program.insert(program.end(), a.b.begin(), a.b.end());
    }
    Result<std::vector<std::uint8_t>> load(std::string_view, std::string_view) override { return program; }
};

} // namespace

TEST_CASE("A0 sub-opcodes load and play game sounds") {
    if (!std::filesystem::exists(obgi_test::game_dir() / "data05500.arc")) return;
    Mixer mixer;
    ArcResourceFinder finder(obgi_test::game_dir());
    SoundHost host{&mixer, &finder};
    vm::SubopRegistry registry;
    vm::register_core_subops(registry);
    register_audio_subops(registry, host);
    OneProgram provider;
    vm::Vm vm(provider, registry);

    auto put = [&](vm::Word addr, const char* s) {
        REQUIRE(vm.global.write(addr, std::strlen(s) + 1, reinterpret_cast<const std::uint8_t*>(s)));
    };
    put(0x100, "data05500.arc");
    put(0x120, "bgm001");
    put(0x140, "data03300.arc");
    put(0x160, "ich_amachoco_a");

    auto run = [&](const Asm& a) {
        provider.set(a);
        vm::ThreadConfig cfg;
        cfg.stack_size = 0x1000;
        cfg.program_capacity = 0x10000;
        REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
        return vm.run_frame(0);
    };

    Asm a;
    a.i32(0x400); // address receiving A0:24's result
    // BGM: pause channel 2 first (as the title script does), load, then play
    a.i32(2).i32(0).sub(0xA0, 0x14);
    a.i32(2).i32(0x100).i32(0x120).i32(0x60).i32(0x40).sub(0xA0, 0x11);
    // SE: load on channel 7, then play at full volume centred
    a.i32(7).i32(0x140).i32(0x160).sub(0xA0, 0x20);
    a.i32(7).i32(0x80).i32(0x40).sub(0xA0, 0x24);
    a.raw({0x09, 0x02}).ret();
    auto st = run(a);
    REQUIRE_MESSAGE(st.has_value(), std::string(st ? "" : st.error().message));
    CHECK(mixer.state(Bank::Stream, 2) == ChannelState::Paused);
    CHECK(mixer.state(Bank::Point, 7) == ChannelState::Playing);
    std::uint8_t res[4];
    REQUIRE(vm.global.read(0x400, 4, res));
    CHECK(load_u32le(res) == 1);

    // resuming the BGM makes sound; both channels contribute
    auto st2 = run(Asm().i32(2).i32(1).sub(0xA0, 0x14).ret());
    REQUIRE(st2.has_value());
    CHECK(mixer.state(Bank::Stream, 2) == ChannelState::Playing);
    std::vector<std::int16_t> out(2 * 4410);
    mixer.mix(out.data(), 4410);
    std::size_t nonzero = 0;
    for (std::int16_t s : out) nonzero += s != 0;
    CHECK(nonzero > 1000);

    // bad channel and bad volume are script errors; so is an unknown file
    CHECK_FALSE(run(Asm().i32(16).i32(0).sub(0xA0, 0x14).ret()).has_value());
    CHECK_FALSE(run(Asm().i32(0).i32(0x81).sub(0xA0, 0x08).ret()).has_value());
    CHECK_FALSE(run(Asm().i32(1).i32(0x100).i32(0x60).sub(0xA0, 0x20).ret()).has_value());
}

TEST_CASE("audio device opens with the dummy driver") {
    SDL_SetHint("SDL_AUDIO_DRIVER", "dummy");
    Mixer mixer;
    auto dev = AudioDevice::open(mixer);
    if (!dev) {
        MESSAGE("no audio device: ", dev.error().message);
        return;
    }
    REQUIRE(mixer.load(Bank::Stream, 0, tone(44100, 1000), false, 0).has_value());
    REQUIRE(mixer.play(Bank::Stream, 0, 0x80, 0x40).has_value());
    SDL_Delay(150); // the dummy device consumes data in real time
    CHECK(mixer.position(Bank::Stream, 0) > 0);
}
