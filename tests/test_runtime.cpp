#include <doctest/doctest.h>
#include <chrono>
#include <thread>
#include "audio/timeline.hpp"
#include "bindings/sequence_subops.hpp"
#include "bindings/system_subops.hpp"
#include "runtime/session.hpp"
#include "services/input.hpp"
#include "services/message.hpp"
#include "services/scene.hpp"
#include "video/player.hpp"
#include "support.hpp"

using namespace obgi;

namespace {
struct Program : vm::ProgramProvider {
    std::map<std::string, std::vector<std::uint8_t>> programs;
    void put(std::string name, std::vector<std::uint8_t> code) {
        std::vector<std::uint8_t> bytes{8, 0, 0, 0};
        auto size = static_cast<std::uint32_t>(code.size());
        for (unsigned i = 0; i < 4; ++i) bytes.push_back(static_cast<std::uint8_t>(size >> (i * 8)));
        bytes.insert(bytes.end(), code.begin(), code.end());
        programs.emplace(std::move(name), std::move(bytes));
    }
    Result<std::vector<std::uint8_t>> load(std::string_view, std::string_view name) override { return programs.at(std::string(name)); }
};
struct Rig {
    Program provider;
    vm::SubopRegistry registry;
    vm::Vm machine{provider, registry};
    Rig() { vm::register_core_subops(registry); }
    vm::Thread& spawn(std::vector<std::uint8_t> code, const std::string& name = "main") {
        provider.put(name, std::move(code));
        vm::ThreadConfig config;
        config.stack_size = 256; config.program_capacity = 4096;
        auto thread = machine.spawn(config, "test", name);
        REQUIRE(thread.has_value());
        return **thread;
    }
};
} // namespace

TEST_CASE("wait completions are checked and delivered exactly once") {
    Rig rig;
    bool complete = false;
    int cleanups = 0, deliveries = 0;
    rig.registry.add({0x9000, "async", 0, 1, [&](vm::Ctx& c) {
        vm::WaitObject wait;
        wait.kind = vm::WaitObject::Kind::Poll;
        wait.cleanup = [&] { ++cleanups; };
        wait.poll = [&](const vm::WaitContext&) -> Result<vm::WaitUpdate> {
            return complete ? vm::WaitUpdate::complete({42}) : vm::WaitUpdate::pending();
        };
        c.suspend(std::move(wait), 1);
        return vm::StayThread;
    }, 1});
    // Store the completed value, yield, then exit.
    auto& thread = rig.spawn({0x90, 0x00, 0x02, 0x00, 0x01, 0, 0, 0x0A, 2, 0x80, 0x5F, 0x17});
    const auto id = thread.id;
    rig.machine.trace_completion = [&](const auto&, auto opcode, const auto& values) {
        CHECK(opcode == 0x9000); CHECK(values == std::vector<vm::Word>{42}); ++deliveries;
    };
    REQUIRE(rig.machine.run_frame(0));
    CHECK(thread.depth == 0);
    complete = true;
    REQUIRE(rig.machine.run_frame(1));
    std::uint8_t result[4]{};
    REQUIRE(rig.machine.global.read(0x100, 4, result));
    CHECK(load_u32le(result) == 42);
    CHECK(cleanups == 1); CHECK(deliveries == 1);
    REQUIRE(rig.machine.run_frame(2));
    CHECK(rig.machine.find(id) == nullptr);
    CHECK(rig.machine.threads().empty());
    CHECK(cleanups == 1);
}

TEST_CASE("malformed asynchronous completion fails before mutating the VM stack") {
    Rig rig;
    rig.registry.add({0x9000, "bad completion", 0, 1, [](vm::Ctx& c) {
        vm::WaitObject wait;
        wait.kind = vm::WaitObject::Kind::Poll;
        wait.poll = [](const auto&) -> Result<vm::WaitUpdate> { return vm::WaitUpdate::complete({1, 2}); };
        c.suspend(std::move(wait), 1);
        return vm::StayThread;
    }, 1});
    auto& thread = rig.spawn({0x90, 0, 0x17});
    auto status = rig.machine.run_frame(0);
    REQUIRE_FALSE(status);
    CHECK(thread.depth == 0);
    CHECK(status.error().message.find("completion stack mismatch") != std::string::npos);
}

TEST_CASE("timer input layers clean up on completion and cancellation") {
    InputState input;
    InputWaitPort port(input);
    Rig rig;
    rig.machine.wait_input = &port;
    auto& thread = rig.spawn({0, 100, 0, 1, 0, 3, 0x80, 0x5C, 0x80, 0x5F, 0x17});
    REQUIRE(rig.machine.run_frame(0));
    CHECK(thread.depth == 0);
    CHECK(input.key_layers.size() == 2);
    input.press_input(1);
    REQUIRE(rig.machine.run_frame(1));
    CHECK(thread.expr[0] == 1);
    CHECK(input.key_layers.size() == 1);
    auto& other = rig.spawn({0, 100, 0, 1, 0, 3, 0x80, 0x5C, 0x17}, "other");
    REQUIRE(rig.machine.run_frame(2));
    other.dead = true;
    rig.machine.collect_dead();
    CHECK(input.key_layers.size() == 1);
}

TEST_CASE("80 5C uses milliseconds and notifications complete with zero") {
    Rig rig;
    auto& thread = rig.spawn({0, 50, 0, 0, 0, 1, 0x80, 0x5C, 0x80, 0x5F, 0x17});
    REQUIRE(rig.machine.run_frame(0));
    REQUIRE(rig.machine.run_frame(49));
    CHECK(thread.depth == 0);
    REQUIRE(rig.machine.run_frame(50));
    CHECK(thread.depth == 1); CHECK(thread.expr[0] == 0);
    auto& notified = rig.spawn({0, 100, 0, 0, 0, 1, 0x80, 0x5C, 0x80, 0x5F, 0x17}, "notified");
    REQUIRE(rig.machine.run_frame(51));
    notified.wait.notified = true;
    REQUIRE(rig.machine.run_frame(52));
    CHECK(notified.expr[0] == 0);
}

TEST_CASE("80 4C message zero notifies the target wait") {
    Rig rig;
    bool notified = false;
    rig.registry.add({0x9000, "interruptible", 0, 1, [&](vm::Ctx& c) {
        vm::WaitObject wait;
        wait.kind = vm::WaitObject::Kind::Poll;
        wait.poll = [&](const vm::WaitContext& context) -> Result<vm::WaitUpdate> {
            notified = context.notified;
            return notified ? vm::WaitUpdate::complete({0}) : vm::WaitUpdate::pending();
        };
        c.suspend(std::move(wait), 1);
        return vm::StayThread;
    }, 1});
    auto& target = rig.spawn({0x90, 0, 0x80, 0x5F, 0x17}, "target");
    const auto target_id = target.id;
    REQUIRE(rig.machine.run_frame(0));
    REQUIRE(target.wait.kind == vm::WaitObject::Kind::Poll);
    auto& sender = rig.spawn({0, static_cast<std::uint8_t>(target_id), 0, 0, 0, 0, 0, 0,
                              0x80, 0x4C, 0x80, 0x5F, 0x17}, "sender");
    REQUIRE(rig.machine.run_frame(1));
    CHECK(sender.expr[0] == 1);
    REQUIRE(rig.machine.run_frame(2));
    CHECK(notified);
    CHECK(target.expr[0] == 0);
}

TEST_CASE("resource completion resumes without advancing the logical frame") {
    Rig rig;
    bool ready = false;
    int hooks = 0, deliveries = 0;
    rig.machine.frame_hook = [&](auto) { ++hooks; };
    rig.machine.trace_completion = [&](const auto&, auto, const auto&) { ++deliveries; };
    rig.registry.add({0x9000, "resource", 0, 1, [&](vm::Ctx& c) {
        vm::WaitObject wait; wait.kind = vm::WaitObject::Kind::Resource;
        wait.poll = [&](const auto&) -> Result<vm::WaitUpdate> {
            return ready ? vm::WaitUpdate::complete({42}) : vm::WaitUpdate::pending();
        };
        c.suspend(std::move(wait), 1); return vm::StayThread;
    }, 1});
    auto& thread = rig.spawn({0x90, 0, 0x80, 0x5F, 0x17});
    REQUIRE(rig.machine.run_frame(0));
    CHECK(thread.wait.kind == vm::WaitObject::Kind::Resource);
    CHECK(rig.machine.frame == 1); CHECK(hooks == 1);
    REQUIRE(rig.machine.resources_ready(1));
    CHECK(thread.depth == 0); CHECK(deliveries == 0);
    ready = true;
    REQUIRE(rig.machine.resources_ready(1));
    CHECK(thread.expr[0] == 42); CHECK(deliveries == 1);
    CHECK(rig.machine.frame == 1); CHECK(hooks == 1);
    REQUIRE(rig.machine.resources_ready(1)); CHECK(deliveries == 1);
}

TEST_CASE("resource completion respects a suppressed frame") {
    Rig rig;
    bool ready = false;
    rig.registry.add({0x9000, "resource", 0, 0, [&](vm::Ctx& c) {
        vm::WaitObject wait; wait.kind = vm::WaitObject::Kind::Resource;
        wait.poll = [&](const auto&) -> Result<vm::WaitUpdate> {
            return ready ? vm::WaitUpdate::complete() : vm::WaitUpdate::pending();
        };
        c.suspend(std::move(wait), 0); return vm::StayThread;
    }});
    auto& thread = rig.spawn({0x90, 0, 0x80, 0x5F, 0x17}, "waiter");
    rig.spawn({0x80, 0x6A, 0x17}, "suppressor");
    REQUIRE(rig.machine.run_frame(0));
    ready = true;
    REQUIRE(rig.machine.resources_ready(1));
    CHECK(thread.wait.kind == vm::WaitObject::Kind::Resource);
    REQUIRE(rig.machine.run_frame(2));
    CHECK(thread.wait.kind == vm::WaitObject::Kind::None);
}

TEST_CASE("resource continuation honors an explicit thread switch") {
    Rig rig;
    bool ready = false;
    rig.registry.add({0x9000, "resource", 0, 0, [&](vm::Ctx& c) {
        vm::WaitObject wait; wait.kind = vm::WaitObject::Kind::Resource;
        wait.poll = [&](const auto&) -> Result<vm::WaitUpdate> { return ready ? vm::WaitUpdate::complete() : vm::WaitUpdate::pending(); };
        c.suspend(std::move(wait), 0); return vm::StayThread;
    }});
    rig.spawn({0x90, 0, 0, 2, 0x80, 0x5E, 0x80, 0x5F, 0x17}, "waiter");
    rig.spawn({0x80, 0x5F, 0, 42, 2, 0, 1, 0, 0, 0x0A, 2, 0x80, 0x5F, 0x17}, "target");
    REQUIRE(rig.machine.run_frame(0));
    ready = true;
    REQUIRE(rig.machine.resources_ready(1));
    std::uint8_t result[4]{}; REQUIRE(rig.machine.global.read(0x100, 4, result));
    CHECK(load_u32le(result) == 42);
    CHECK(rig.machine.frame == 1);
}

TEST_CASE("resource wait can be cancelled by a thread notification") {
    Rig rig;
    rig.registry.add({0x9000, "resource", 0, 0, [](vm::Ctx& c) {
        vm::WaitObject wait; wait.kind = vm::WaitObject::Kind::Resource;
        wait.poll = [](const auto&) -> Result<vm::WaitUpdate> { return vm::WaitUpdate::pending(); };
        c.suspend(std::move(wait), 0); return vm::StayThread;
    }});
    auto& thread = rig.spawn({0x90, 0, 0x80, 0x5F, 0x17}, "waiter");
    rig.spawn({0, 1, 0, 0, 0, 0, 0, 0, 0x80, 0x4C, 0x80, 0x5F, 0x17}, "notifier");
    REQUIRE(rig.machine.run_frame(0));
    CHECK(thread.wait.kind == vm::WaitObject::Kind::Resource);
    REQUIRE(rig.machine.run_frame(1));
    CHECK(thread.wait.kind == vm::WaitObject::Kind::None);
}

TEST_CASE("switching to a missing thread ends the scheduler pass") {
    Rig rig;
    auto& first = rig.spawn({0, 99, 0x80, 0x5E, 0x17});
    auto& second = rig.spawn({0x80, 0x5F, 0x17}, "second");
    REQUIRE(rig.machine.run_frame(0));
    CHECK(first.ip == 4); CHECK(second.ip == 0);
    REQUIRE(rig.machine.run_frame(1));
}

TEST_CASE("registry rejects ambiguous opcode ownership") {
    vm::SubopRegistry registry;
    registry.add({1, "first", 0, 0, [](auto&) { return 0; }});
    CHECK_THROWS_AS(registry.add({1, "second", 0, 0, [](auto&) { return 0; }}), std::logic_error);
}

TEST_CASE("message progress follows elapsed time independently of update frequency") {
    MessageService service;
    MessagePacing pacing;
    pacing.character_ms = 25;
    MessageVisual visual;
    visual.total = 10;
    auto frequent = service.start(visual, pacing, 0, 0);
    auto sparse = frequent;
    for (int ms = 0; ms < 173; ++ms) (void)frequent.update(ms, 0);
    (void)frequent.update(173, 0);
    (void)sparse.update(0, 0);
    (void)sparse.update(173, 0);
    CHECK(frequent.progress.shown == 6);
    CHECK(sparse.progress.shown == frequent.progress.shown);
    CHECK(sparse.update(250, 0) == 0);
}

TEST_CASE("rich message repaints at most once per visual frame and completes on time") {
    MessageService service;
    std::vector<std::size_t> draws;
    std::vector<int> pre;
    MessageVisual visual; visual.rich = true; visual.total = 51; visual.pre_frames = 2;
    visual.draw = [&](std::size_t n) { draws.push_back(n); };
    visual.pre = [&](int n, int) { pre.push_back(n); };
    MessagePacing pacing; pacing.visual_interval_us = 16000;
    auto op = service.start(visual, pacing, 0, 0);
    for (int ms = 0; ms < 83; ++ms) CHECK_FALSE(op.update(ms, 0));
    CHECK(op.update(83, 0) == 0);
    CHECK(pre == std::vector<int>{0, 1});
    CHECK(draws == std::vector<std::size_t>{16, 32, 48, 51});
}

TEST_CASE("rich message adapts when the presentation interval changes") {
    MessageService service;
    std::vector<std::size_t> draws;
    MessageVisual visual; visual.rich = true; visual.total = 100;
    visual.draw = [&](std::size_t n) { draws.push_back(n); };
    MessagePacing pacing; pacing.visual_interval_us = 8334;
    auto op = service.start(visual, pacing, 0, 0);
    CHECK_FALSE(op.update(10, 0));
    CHECK_FALSE(op.update(20, 0));
    op.pacing.visual_interval_us = 16667;
    CHECK_FALSE(op.update(25, 0));
    CHECK_FALSE(op.update(34, 0));
    CHECK(draws == std::vector<std::size_t>{8, 16, 33});
    CHECK(op.update(100, 0) == 0);
    CHECK(draws.back() == 100);
}

TEST_CASE("a reveal click cannot also dismiss the message") {
    MessageService service;
    MessagePacing pacing;
    pacing.allow_skip = true; pacing.wait_click = true;
    MessageVisual visual; visual.total = 20;
    auto op = service.start(visual, pacing, 0, 0);
    CHECK_FALSE(op.update(0, 0));
    CHECK_FALSE(op.update(1, 1));
    CHECK(op.progress.shown == 20);
    CHECK(op.update(2, 2) == 1);
}

TEST_CASE("a panel click does not advance a waiting message") {
    InputState input;
    MessageInput message_input(input);
    MessageService service;
    MessagePacing pacing; pacing.wait_click = true;
    MessageVisual visual; visual.total = 0;
    auto message = service.start(visual, pacing, 0, 0);
    input.press_input(1);
    REQUIRE(consume_input_trigger(input, 1)); // the panel handled this click first
    CHECK_FALSE(message.update(1, message_input.poll(input)));
    input.press_input(1);
    CHECK(message.update(2, message_input.poll(input)) == 1);
}

TEST_CASE("automatic message dismissal does not drift when updates are sparse") {
    MessageService service;
    MessagePacing pacing;
    pacing.character_ms = 25; pacing.wait_click = true; pacing.automatic = true; pacing.auto_ms = 100;
    MessageVisual visual; visual.total = 4;
    auto op = service.start(visual, pacing, 0, 0);
    CHECK(op.update(200, 0) == 1);
}

TEST_CASE("logical scene copies preserve transforms and creation ordering without GPU state") {
    SceneState scene;
    auto& parent = scene.sprite(1); parent.x = 50; parent.y = 60;
    auto& child = scene.sprite(2); child.has_master = true; child.master = 1; child.master_dx = 7; child.master_dy = 8;
    auto restored = scene;
    restored.resolve_masters();
    CHECK(restored.find_sprite(2)->x == 57);
    CHECK(restored.find_sprite(2)->y == 68);
    CHECK(scene.find_sprite(2)->x == 0);
    CHECK(restored.creation_order(1) < restored.creation_order(2));
}

TEST_CASE("silent audio advances exact sample counts across irregular scheduling") {
    auto pcm = std::make_shared<Pcm>();
    pcm->sample_rate = 44100; pcm->channels = 1; pcm->samples.resize(44100);
    Mixer mixer;
    REQUIRE(mixer.load(Bank::Point, 0, pcm, false, 0));
    REQUIRE(mixer.play(Bank::Point, 0, kMaxVolume, kCenterPan));
    AudioTimeline timeline(mixer);
    for (int ms = 0; ms <= 999; ms += 3) timeline.advance(ms);
    timeline.advance(1000);
    CHECK(mixer.position(Bank::Point, 0) == 44100);
    timeline.advance(1001);
    CHECK(mixer.state(Bank::Point, 0) == ChannelState::Stopped);
}

TEST_CASE("deferred sequences reject freed targets and survive owner cancellation" * doctest::test_suite("game")) {
    REQUIRE(std::filesystem::exists(obgi_test::game_dir() / "autoload.arc"));
    RendererOptions options; options.headless = true;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer); REQUIRE(display);
    ArcResourceFinder resources(obgi_test::game_dir());
    const auto rows = obgi_test::read_tsv("bm_manifest.tsv"); REQUIRE_FALSE(rows.empty());
    auto bytes = resources.read(rows[0][0], rows[0][1]); REQUIRE(bytes);
    SequenceService service(**display, true);
    auto info = service.load(std::move(*bytes)); REQUIRE(info);
    const auto format = info->mode == 2 ? SlotFormat::Bgra : SlotFormat::Bgrx;
    REQUIRE((*display)->create_slot(1, info->width, info->height, format));
    auto request = service.decode(1, info->id, 0, true); REQUIRE(request.operation);
    REQUIRE((*display)->free_slot(1));
    REQUIRE((*display)->create_slot(1, info->width, info->height, format));
    CHECK(service.poll(*request.operation) == 5);
    request = service.decode(1, info->id, 0, true); REQUIRE(request.operation);
    REQUIRE(service.release(info->id));
    CHECK(service.poll(*request.operation) == 3);
    // Exercise the real opcode's split stack contract against the service.
    bytes = resources.read(rows[0][0], rows[0][1]); REQUIRE(bytes);
    info = service.load(std::move(*bytes)); REQUIRE(info);
    REQUIRE(info->id < 128);
    Rig rig;
    register_sequence_subops(rig.registry, service, resources);
    rig.spawn({0x80, 0x53, 0, 1, 0, static_cast<std::uint8_t>(info->id), 0, 0, 0x90, 0xF6, 0x80, 0x5F, 0x17});
    REQUIRE(rig.machine.run_frame(0));
    REQUIRE(rig.machine.find(1));
    CHECK(rig.machine.find(1)->depth == 1);
    CHECK(rig.machine.find(1)->expr[0] == 0);
    // Real worker completion cannot write to a replacement bitmap either.
    SequenceService background(**display);
    bytes = resources.read(rows[0][0], rows[0][1]); REQUIRE(bytes);
    info = background.load(std::move(*bytes)); REQUIRE(info);
    request = background.decode(1, info->id, 0, true); REQUIRE(request.operation);
    REQUIRE((*display)->free_slot(1));
    REQUIRE((*display)->create_slot(1, info->width, info->height, format));
    std::optional<std::uint32_t> completed;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!(completed = background.poll(*request.operation)) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(completed);
    CHECK(*completed == 5);
}

TEST_CASE("movie playback advances through session ticks on the configured canvas" * doctest::test_suite("game")) {
    RendererOptions options; options.headless = true; options.logical_width = 1280; options.logical_height = 720;
    auto renderer = Renderer::create(options); REQUIRE(renderer);
    Mixer mixer;
    AudioTimeline audio(mixer);
    MoviePlayer player(**renderer, mixer);
    ArcResourceFinder resources(obgi_test::game_dir());
    auto file = resources.read("data02280", "ac2_op.mpg"); REQUIRE(file);
    REQUIRE(player.start(std::move(*file), "ac2_op.mpg", 0));
    REQUIRE(player.active());
    REQUIRE(player.update(0));
    audio.advance(100);
    REQUIRE(player.update(100));
    REQUIRE(player.present());
    auto pixels = (*renderer)->read_back(); REQUIRE(pixels);
    CHECK(pixels->size() == 1280u * 720u * 4u);
    CHECK(mixer.position(Bank::Point, kMovieChannel) > 0);
    CHECK_FALSE(player.result());
    player.stop(MovieEnd::Skipped);
    CHECK(player.result() == MovieEnd::Skipped);
    CHECK(mixer.state(Bank::Point, kMovieChannel) == ChannelState::Empty);
}

TEST_CASE("game session boots, restarts and uses isolated persistent state" * doctest::test_suite("game")) {
    REQUIRE(std::filesystem::exists(obgi_test::game_dir() / "autoload.arc"));
    SessionOptions options;
    options.game_dir = obgi_test::game_dir();
    options.data_dir = std::filesystem::temp_directory_path() / "obgi_session_regression";
    auto session = EngineSession::create(options); REQUIRE(session);
    REQUIRE((*session)->boot());
    for (int frame = 0; frame < 50; ++frame) {
        auto status = (*session)->tick(frame * 16);
        REQUIRE_MESSAGE(status.has_value(), std::string(status ? "" : status.error().message));
    }
    CHECK_FALSE((*session)->tick(1));
    REQUIRE((*session)->restart());
    CHECK((*session)->machine().frame == 0);
    REQUIRE((*session)->tick(0));
}

TEST_CASE("startup brand fade has intermediate pixels from its priority snapshots" * doctest::test_suite("game")) {
    SessionOptions options;
    options.game_dir = obgi_test::game_dir();
    options.data_dir = std::filesystem::temp_directory_path() / "obgi_fade_regression";
    auto created = EngineSession::create(options); REQUIRE(created);
    auto& session = **created;
    REQUIRE(session.boot());
    std::optional<std::int64_t> start;
    std::vector<std::uint64_t> brightness;
    for (int frame = 0; frame < 400; ++frame) {
        const auto now = frame * std::int64_t{16};
        REQUIRE(session.tick(now));
        if (!start) {
            for (const auto& motion : session.system().motions)
                if (motion.object == 0 && motion.duration_ms == 1500 && motion.from_level == 256 && motion.to_level == 0)
                    start = motion.start_ms;
        }
        if (!start) continue;
        const auto elapsed = now - *start;
        const bool sample = brightness.empty() || (brightness.size() == 1 && elapsed >= 750) ||
                            (brightness.size() == 2 && elapsed >= 1500);
        if (!sample) continue;
        auto image = session.capture(); REQUIRE(image);
        std::uint64_t sum = 0;
        for (std::size_t i = 0; i < image->size(); i += 113)
            sum += ((*image)[i] & 255) + (((*image)[i] >> 8) & 255) + (((*image)[i] >> 16) & 255);
        brightness.push_back(sum);
        if (brightness.size() == 3) break;
    }
    REQUIRE(brightness.size() == 3);
    CHECK(brightness[1] > brightness[0]);
    CHECK(brightness[2] > brightness[1]);
}
