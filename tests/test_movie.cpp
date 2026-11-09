#include <doctest/doctest.h>

#include <cstring>
#include <filesystem>
#include <thread>
#include <chrono>

#include "audio/pcm.hpp"
#include "bindings/movie_subops.hpp"
#include "support.hpp"
#include "video/movie.hpp"
#include "vfs/arc.hpp"
#include "vm/vm.hpp"

using namespace obgi;

namespace {

bool have_movies() { return std::filesystem::exists(obgi_test::game_dir() / "data02280.arc"); }

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

TEST_CASE("the opening movie decodes through FFmpeg" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    auto arc = Arc::open(obgi_test::game_dir() / "data02280.arc");
    REQUIRE(arc.has_value());
    const ArcEntry* e = arc->find("ac2_op.mpg");
    REQUIRE(e != nullptr);
    ByteView file = arc->data(*e);

    auto dec = MovieDecoder::open(file, "ac2_op.mpg");
    REQUIRE_MESSAGE(dec.has_value(), std::string(dec ? "" : dec.error().message));
    CHECK((*dec)->width() == 1280);
    CHECK((*dec)->height() == 720);
    CHECK((*dec)->fps() == doctest::Approx(30.0).epsilon(0.01));

    MovieDecoder::Frame f;
    double prev = -1;
    bool any_picture = false, picture_changes = false;
    std::vector<std::uint8_t> first;
    for (int i = 0; i < 90; ++i) {
        auto more = (*dec)->next(f);
        REQUIRE(more.has_value());
        REQUIRE(*more);
        REQUIRE(f.bgra.size() == 1280u * 720u * 4u);
        CHECK(f.pts > prev);
        prev = f.pts;
        if (i == 0) first.assign(f.bgra.begin(), f.bgra.end());
        else if (!picture_changes && std::memcmp(first.data(), f.bgra.data(), first.size()) != 0) picture_changes = true;
        for (std::size_t p = 0; p < f.bgra.size() && !any_picture; p += 4 * 997) any_picture = f.bgra[p] || f.bgra[p + 1] || f.bgra[p + 2];
        CHECK(f.bgra[3] == 255);
    }
    CHECK(any_picture);
    CHECK(picture_changes);
    CHECK(prev == doctest::Approx(89.0 / 30.0).epsilon(0.05));
}

TEST_CASE("the movie's sound track decodes with the audio path" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    auto arc = Arc::open(obgi_test::game_dir() / "data02280.arc");
    REQUIRE(arc.has_value());
    const ArcEntry* e = arc->find("ac2_op.mpg");
    REQUIRE(e != nullptr);
    auto pcm = decode_audio(arc->data(*e), "ac2_op.mpg");
    REQUIRE_MESSAGE(pcm.has_value(), std::string(pcm ? "" : pcm.error().message));
    CHECK((*pcm)->channels == 2);
    CHECK((*pcm)->sample_rate >= 32000);
    CHECK((*pcm)->frames() > (*pcm)->sample_rate * 10u); // an opening is longer than ten seconds
}

TEST_CASE("incremental movie audio matches the full decoder" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    auto arc = Arc::open(obgi_test::game_dir() / "data02280.arc"); REQUIRE(arc);
    const auto* entry = arc->find("ac2_op.mpg"); REQUIRE(entry);
    auto full = decode_audio(arc->data(*entry), "ac2_op.mpg"); REQUIRE(full);
    auto stream = StreamAudioDecoder::open(arc->data(*entry), "ac2_op.mpg", (*full)->sample_rate); REQUIRE(stream);
    std::vector<std::int16_t> samples;
    while (samples.size() < 44100) {
        auto block = (*stream)->next(); REQUIRE(block); REQUIRE_FALSE(block->empty());
        samples.insert(samples.end(), block->begin(), block->end());
    }
    REQUIRE((*full)->channels == 2); REQUIRE((*full)->samples.size() >= samples.size());
    CHECK(std::equal(samples.begin(), samples.end(), (*full)->samples.begin()));
}

TEST_CASE("realtime movie workers stop safely and can be restarted" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    ArcResourceFinder finder(obgi_test::game_dir());
    auto media = finder.open_media("data02280", "ac2_op.mpg"); REQUIRE(media);
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    Mixer mixer; MoviePlayer player(**renderer, mixer);
    for (int repeat = 0; repeat < 3; ++repeat) {
        REQUIRE(player.start(*media, "ac2_op.mpg", 0, std::nullopt, true));
        CHECK(player.active()); CHECK(player.duration_ms() > 10000);
        std::vector<std::int16_t> samples(4096);
        for (int frame = 0; frame < 8; ++frame) {
            mixer.mix(samples.data(), 2048);
            REQUIRE(player.update(frame * 47));
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        player.stop();
        CHECK_FALSE(player.active());
        CHECK(player.result() == MovieEnd::Skipped);
        CHECK(mixer.state(Bank::Point, kMovieChannel) == ChannelState::Empty);
    }
}

TEST_CASE("movie preparation is asynchronous, cancellable and restartable" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    ArcResourceFinder finder(obgi_test::game_dir());
    auto media = finder.open_media("data02280", "ac2_op.mpg"); REQUIRE(media);
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    Mixer mixer; MoviePlayer player(**renderer, mixer);
    REQUIRE(player.start_async(*media, "ac2_op.mpg"));
    CHECK(player.active()); CHECK(player.preparing());
    player.stop();
    CHECK_FALSE(player.active());
    REQUIRE(player.start_async(*media, "ac2_op.mpg"));
    for (int i = 0; i < 1000 && player.preparing(); ++i) {
        REQUIRE(player.update(i * 2));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK_FALSE(player.preparing());
    CHECK(player.active()); CHECK(player.duration_ms() > 10000);
    player.stop();
    auto bad = std::make_shared<std::vector<std::uint8_t>>(100, 0);
    REQUIRE(player.start_async(OwnedBytes{bad, ByteView(*bad)}, "broken.mpg"));
    for (int i = 0; i < 1000 && player.preparing(); ++i) {
        REQUIRE(player.update(i * 2));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK_FALSE(player.active());
    CHECK(player.result() == MovieEnd::Failed);
}

TEST_CASE("archive movie opcode resumes with duration after async preparation" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    Mixer mixer; MoviePlayer player(**renderer, mixer);
    ArcResourceFinder finder(obgi_test::game_dir());
    MovieHost host; host.resources = &finder; host.player = &player; host.realtime = true;
    vm::SubopRegistry registry; vm::register_core_subops(registry); register_movie_subops(registry, host);
    OneProgram provider; vm::Vm machine(provider, registry);
    const char arc[] = "data02280.arc", name[] = "ac2_op.mpg";
    REQUIRE(machine.global.write(0x100, sizeof arc, reinterpret_cast<const std::uint8_t*>(arc)));
    REQUIRE(machine.global.write(0x120, sizeof name, reinterpret_cast<const std::uint8_t*>(name)));
    Asm a; a.i32(0x400).i32(0x100).i32(0x120).i32(0).i32(0).i32(1280).i32(720)
        .sub(0x92, 0xF0).raw({0x09, 0x02}).ret();
    provider.set(a); vm::ThreadConfig cfg; cfg.stack_size = 1024;
    auto thread = machine.spawn(cfg, "x.arc", "main"); REQUIRE(thread);
    const auto id = (*thread)->id;
    REQUIRE(machine.run_frame(0));
    CHECK(player.preparing()); CHECK(machine.find(id));
    for (int i = 1; i < 1000 && machine.find(id); ++i) {
        REQUIRE(player.update(i * 2)); REQUIRE(machine.run_frame(i * 2));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK_FALSE(machine.find(id)); CHECK(player.active());
    std::uint8_t result[4]; REQUIRE(machine.global.read(0x400, 4, result));
    CHECK(load_u32le(result) > 10000);
    player.stop();
}

TEST_CASE("91:F8 plays a movie from the archive" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    ArcResourceFinder finder(obgi_test::game_dir());
    std::size_t played_bytes = 0;
    MovieHost host;
    host.resources = &finder;
    host.play = [&](ByteView file, const std::string&) -> Result<MovieEnd> {
        played_bytes = file.size();
        return MovieEnd::Finished;
    };
    vm::SubopRegistry registry;
    vm::register_core_subops(registry);
    register_movie_subops(registry, host);
    OneProgram provider;
    vm::Vm vm(provider, registry);
    auto put = [&](vm::Word addr, const char* s) {
        REQUIRE(vm.global.write(addr, std::strlen(s) + 1, reinterpret_cast<const std::uint8_t*>(s)));
    };
    put(0x100, "data02280.arc");
    put(0x120, "ac2_op.mpg");
    put(0x140, "no_such.mpg");
    auto status_of = [&](vm::Word name) {
        Asm a;
        a.i32(0x400).i32(0x100).i32(name).i32(0).sub(0x91, 0xF8).raw({0x09, 0x02}).ret();
        provider.set(a);
        vm::ThreadConfig cfg;
        cfg.stack_size = 0x1000;
        cfg.program_capacity = 0x10000;
        REQUIRE(vm.spawn(cfg, "x.arc", "main").has_value());
        auto st = vm.run_frame(0);
        REQUIRE_MESSAGE(st.has_value(), std::string(st ? "" : st.error().message));
        std::uint8_t res[4];
        REQUIRE(vm.global.read(0x400, 4, res));
        return load_u32le(res);
    };
    CHECK(status_of(0x120) == 0);
    CHECK(played_bytes > 1000000);
    played_bytes = 0;
    CHECK(status_of(0x140) == 0xFFFFFFFDu); // file not found
    CHECK(played_bytes == 0);

    host.play = [](ByteView, const std::string&) -> Result<MovieEnd> { return fail(Errc::Format, "broken"); };
    CHECK(status_of(0x120) == 0xFFFFFFFFu);
}

TEST_CASE("archive movie starts, reports duration, and stops" * doctest::test_suite("game")) {
    REQUIRE(have_movies());
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    Mixer mixer;
    MoviePlayer player(**renderer, mixer);
    ArcResourceFinder finder(obgi_test::game_dir());
    MovieHost host; host.resources = &finder; host.player = &player;
    vm::SubopRegistry registry;
    vm::register_core_subops(registry);
    register_movie_subops(registry, host);
    OneProgram provider;
    vm::Vm machine(provider, registry);
    auto put = [&](vm::Word addr, const char* text) {
        REQUIRE(machine.global.write(addr, std::strlen(text) + 1, reinterpret_cast<const std::uint8_t*>(text)));
    };
    put(0x100, "data02280.arc"); put(0x120, "ac2_op.mpg");
    Asm program;
    program.i32(0x400).i32(0x100).i32(0x120).i32(0).i32(0).i32(1280).i32(720).sub(0x92, 0xF0).raw({0x09, 0x02});
    program.i32(0x404).sub(0x90, 0xF2).raw({0x09, 0x02});
    program.sub(0x90, 0xF1);
    program.i32(0x408).sub(0x90, 0xF2).raw({0x09, 0x02}).ret();
    provider.set(program);
    vm::ThreadConfig cfg; cfg.stack_size = 0x1000; cfg.program_capacity = 0x10000;
    REQUIRE(machine.spawn(cfg, "x.arc", "main"));
    REQUIRE(machine.run_frame(0));
    std::uint8_t out[12]; REQUIRE(machine.global.read(0x400, sizeof out, out));
    CHECK(load_u32le(out) > 10000);
    CHECK(load_u32le(out + 4) == 1);
    CHECK(load_u32le(out + 8) == 0);
}
