#include <doctest/doctest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include "resources/decoded_cache.hpp"
#include "bindings/graphics_subops.hpp"
#include "bindings/audio_subops.hpp"

using namespace obgi;
namespace {
struct Files {
    std::filesystem::path path;
    Files() {
        static std::atomic<unsigned> serial{};
        path = std::filesystem::temp_directory_path() / ("obgi_async_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" + std::to_string(serial++));
        std::filesystem::create_directory(path);
    }
    ~Files() { std::error_code ec; std::filesystem::remove_all(path, ec); }
    void write(const std::string& name, const std::vector<std::uint8_t>& bytes) {
        std::ofstream out(path / name, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size())); REQUIRE(out.good());
    }
    void arc(const std::string& member, const std::vector<std::uint8_t>& payload) {
        std::vector<std::uint8_t> bytes(144, 0);
        std::memcpy(bytes.data(), "BURIKO ARC20", 12); bytes[12] = 1;
        std::memcpy(bytes.data() + 16, member.data(), member.size());
        auto size = static_cast<unsigned>(payload.size());
        for (unsigned i = 0; i < 4; ++i) bytes[16 + 0x64 + i] = size >> (i * 8);
        bytes.insert(bytes.end(), payload.begin(), payload.end()); write("data.arc", bytes);
    }
};
using Bytes = std::vector<std::uint8_t>;
using Cache = DecodedCache<Bytes>;
Cache::Decoder decoder(unsigned& calls) {
    return [&calls](OwnedBytes raw) -> Result<Cache::Pointer> {
        ++calls;
        auto bytes = unpack_resource(std::move(raw));
        if (!bytes) return std::unexpected(bytes.error());
        return std::make_shared<const Bytes>(bytes->bytes.begin(), bytes->bytes.end());
    };
}
auto byte_size = [](const Bytes& bytes) { return bytes.size(); };
struct Code {
    Bytes bytes;
    Code& i(std::uint32_t value) { bytes.push_back(2); for (int i = 0; i < 4; ++i) bytes.push_back(value >> (i * 8)); return *this; }
    Code& op(unsigned a, unsigned b) { bytes.push_back(a); bytes.push_back(b); return *this; }
    Code& store(unsigned address, unsigned value) { return i(address).i(value).op(9, 2); }
    Code& ret() { bytes.push_back(0x17); return *this; }
};
struct Programs : vm::ProgramProvider {
    Bytes bytes;
    void set(const Code& code) {
        bytes = {8, 0, 0, 0};
        auto size = static_cast<unsigned>(code.bytes.size());
        for (int i = 0; i < 4; ++i) bytes.push_back(size >> (i * 8));
        bytes.insert(bytes.end(), code.bytes.begin(), code.bytes.end());
    }
    Result<Bytes> load(std::string_view, std::string_view) override { return bytes; }
};
void strings(vm::Vm& machine) {
    const std::uint8_t data[] = "data\0item";
    REQUIRE(machine.global.write(0x100, sizeof data, data));
}
unsigned word(vm::Vm& machine, unsigned address) {
    std::uint8_t bytes[4]; REQUIRE(machine.global.read(address, 4, bytes)); return load_u32le(bytes);
}
std::vector<std::uint8_t> image() {
    Bytes data(20, 0); data[0] = data[2] = 1; data[4] = 32;
    data[16] = 0x56; data[17] = 0x34; data[18] = 0x12; data[19] = 0xff;
    return data;
}
Bytes sound() {
    Bytes b(64 + 44 + 128, 0);
    auto u16 = [&](unsigned off, unsigned v) { b[off] = v; b[off + 1] = v >> 8; };
    auto u32 = [&](unsigned off, unsigned v) { for (unsigned i = 0; i < 4; ++i) b[off + i] = v >> (8 * i); };
    u32(0, 64); std::memcpy(b.data() + 4, "bw  ", 4);
    u32(8, 172); u32(12, 64); u32(16, 44100); u32(20, 1);
    std::memcpy(b.data() + 64, "RIFF", 4); u32(68, 164);
    std::memcpy(b.data() + 72, "WAVEfmt ", 8); u32(80, 16);
    u16(84, 1); u16(86, 1); u32(88, 44100); u32(92, 88200); u16(96, 2); u16(98, 16);
    std::memcpy(b.data() + 100, "data", 4); u32(104, 128);
    for (unsigned off = 108; off < b.size(); off += 2) u16(off, 12000);
    return b;
}
}

TEST_CASE("decode admission bounds queued work without inline fallback") {
    Files files; files.arc("item", {1, 2, 3}); files.write("other", {4});
    ArcResourceFinder finder(files.path); DecodeQueue queue(0, 1, 1);
    unsigned calls = 0; Cache cache(finder, decoder(calls), byte_size, 16, &queue);
    auto first = cache.request("data", "item"); REQUIRE(first);
    auto shared = cache.request("DATA", "ITEM"); REQUIRE(shared);
    CHECK(first->get()->task == shared->get()->task);
    CHECK(queue.stats().admitted == 1); CHECK(queue.stats().queued == 1);
    auto blocked = cache.request("data", "other"); REQUIRE(blocked);
    CHECK_FALSE(cache.poll(*blocked)); CHECK(calls == 0);
    REQUIRE(queue.run_one()); CHECK(calls == 1);
    auto ready = cache.poll(*first); REQUIRE(ready); REQUIRE(*ready); CHECK(***ready == Bytes{1, 2, 3});
    CHECK(queue.stats().admitted == 0); // completed consumers cannot starve other script threads
    CHECK_FALSE(cache.poll(*blocked)); CHECK(queue.stats().queued == 1); CHECK(calls == 1);
    first->reset(); shared->reset();
    REQUIRE(queue.run_one()); auto second = cache.poll(*blocked); REQUIRE(second); REQUIRE(*second);
    CHECK(***second == Bytes{4}); CHECK(calls == 2);
    auto hit = cache.request("data", "item"); REQUIRE(hit); REQUIRE(cache.poll(*hit)); CHECK(calls == 2);
}

TEST_CASE("cancelled decode consumers do not run queued work or poison future requests") {
    Files files; files.arc("item", {1}); ArcResourceFinder finder(files.path);
    DecodeQueue queue(0, 1); unsigned calls = 0; Cache cache(finder, decoder(calls), byte_size, 16, &queue);
    auto request = cache.request("data", "item"); REQUIRE(request); request->reset();
    auto replacement = cache.request("data", "item"); REQUIRE(replacement);
    REQUIRE(queue.run_one()); CHECK(calls == 0); // abandoned job was skipped
    CHECK_FALSE(cache.poll(*replacement)); REQUIRE(queue.run_one());
    auto ready = cache.poll(*replacement); REQUIRE(ready); REQUIRE(*ready); CHECK(calls == 1);
    CHECK(cache.count() == 1);
}

TEST_CASE("raw mapping defers DSC errors to the decoder and cache clear rejects old publication") {
    Files files; Bytes bad(20, 0); std::memcpy(bad.data(), "DSC FORMAT 1.00", 15); files.arc("item", bad);
    ArcResourceFinder finder(files.path); REQUIRE(finder.open_raw("data", "item")); CHECK_FALSE(finder.open_media("data", "item"));
    DecodeQueue queue(0); unsigned calls = 0; Cache cache(finder, decoder(calls), byte_size, 64, &queue);
    auto request = cache.request("data", "item"); REQUIRE(request); CHECK(calls == 0);
    REQUIRE(queue.run_one()); auto result = cache.poll(*request); REQUIRE(result); CHECK_FALSE(*result);
    CHECK(cache.count() == 0);
    Files good; good.arc("item", {9}); ArcResourceFinder other(good.path);
    Cache clean(other, decoder(calls), byte_size, 64, &queue);
    auto old = clean.request("data", "item"); REQUIRE(old); clean.clear(); REQUIRE(queue.run_one());
    auto ready = clean.poll(*old); REQUIRE(ready); REQUIRE(*ready); CHECK(clean.count() == 0);
}

TEST_CASE("decode shutdown discards queued jobs while detached consumers remain safe") {
    unsigned calls = 0;
    std::shared_ptr<DecodeQueue::Permit> permit;
    { DecodeQueue queue(0); permit = queue.try_submit(64, [&] { ++calls; }); REQUIRE(permit); }
    CHECK(calls == 0); permit.reset();
}

TEST_CASE("async bitmap loading suspends only its caller and rejects replaced targets" * doctest::test_suite("gpu")) {
    for (int action : {0, 1, 2, 3}) { // complete, replace, free before allocation, kill the caller
        Files files; files.arc("item", image()); ArcResourceFinder finder(files.path); DecodeQueue queue(0);
        ArcImageSource images(finder); images.set_queue(&queue);
        RendererOptions options; options.headless = true; auto renderer = Renderer::create(options); REQUIRE(renderer);
        auto display = Display::create(**renderer, 8, 8); REQUIRE(display); auto& d = **display;
        GraphicsHost host{&d, &images}; vm::SubopRegistry registry; register_graphics_subops(registry, host);
        Programs programs; programs.set(Code().i(7).i(0x100).i(0x105).op(0x90, 0x10).store(0x200, 123).ret());
        vm::Vm machine(programs, registry); strings(machine);
        auto loading = machine.spawn({}, "x", "load"); REQUIRE(loading);
        programs.set(Code().store(0x204, 456).ret()); REQUIRE(machine.spawn({}, "x", "other"));
        REQUIRE(machine.run_frame(0)); CHECK(word(machine, 0x200) == 0); CHECK(word(machine, 0x204) == 456);
        CHECK((*loading)->wait.kind == vm::WaitObject::Kind::Poll); CHECK(d.slot(7) == nullptr);
        if (action == 1) { REQUIRE(d.create_slot(7, 1, 1, SlotFormat::Bgra)); REQUIRE(d.fill_slot(7, 0xffaabbcc)); }
        if (action == 2) CHECK_FALSE(d.free_slot(7)); // even an unallocated slot invalidates its pending load
        if (action == 3) { (*loading)->dead = true; REQUIRE(machine.run_frame(1)); }
        REQUIRE(queue.run_one()); REQUIRE(machine.run_frame(2));
        CHECK(word(machine, 0x200) == (action == 3 ? 0u : 123u));
        if (action >= 2) CHECK(d.slot(7) == nullptr);
        else { auto pixels = d.read_slot(7); REQUIRE(pixels); CHECK((*pixels)[0] == (action == 1 ? 0xffaabbccu : 0xff123456u)); }
    }
}

TEST_CASE("async audio preserves pause and stop invalidates pending loads") {
    for (bool stop : {false, true}) {
        Files files; files.arc("item", sound()); ArcResourceFinder finder(files.path); DecodeQueue queue(0);
        Mixer mixer; REQUIRE(mixer.set_paused(Bank::Stream, 2, true));
        SoundHost host{&mixer, &finder, &queue}; vm::SubopRegistry registry; register_audio_subops(registry, host);
        Programs programs; programs.set(Code().i(2).i(0x100).i(0x105).i(0x80).i(0x40).op(0xa0, 0x11).store(0x200, 1).ret());
        vm::Vm machine(programs, registry); strings(machine); REQUIRE(machine.spawn({}, "x", "sound"));
        REQUIRE(machine.run_frame(0)); CHECK(word(machine, 0x200) == 0);
        if (stop) REQUIRE(mixer.stop(Bank::Stream, 2));
        REQUIRE(queue.run_one()); REQUIRE(machine.run_frame(1)); CHECK(word(machine, 0x200) == 1);
        CHECK(mixer.state(Bank::Stream, 2) == (stop ? ChannelState::Empty : ChannelState::Paused));
        if (!stop) {
            REQUIRE(mixer.start(Bank::Stream, 2)); REQUIRE(mixer.set_paused(Bank::Stream, 2, false));
            std::int16_t output[8]{}; mixer.mix(output, 4); CHECK(output[0] > 10000);
        }
    }
}

TEST_CASE("a cancelled duplicate leaves another consumer's background request alive") {
    Files files; files.arc("item", {7}); ArcResourceFinder finder(files.path);
    DecodeQueue queue(0); unsigned calls = 0; Cache cache(finder, decoder(calls), byte_size, 16, &queue);
    auto first = cache.request("data", "item"), second = cache.request("data", "item"); REQUIRE(first); REQUIRE(second);
    first->reset(); REQUIRE(queue.run_one()); auto result = cache.poll(*second); REQUIRE(result); REQUIRE(*result);
    CHECK(***result == Bytes{7}); CHECK(calls == 1);
}

TEST_CASE("intro and loop joining completes on a saturated single-job queue") {
    Files files; files.arc("item", sound()); files.write("body", sound());
    ArcResourceFinder finder(files.path); DecodeQueue queue(0, 1); Mixer mixer;
    SoundHost host{&mixer, &finder, &queue}; vm::SubopRegistry registry; register_audio_subops(registry, host);
    Programs programs;
    programs.set(Code().i(2).i(0x100).i(0x105).i(0x120).i(1).i(0x80).i(0x40).op(0xa0, 0x12).store(0x200, 9).ret());
    vm::Vm machine(programs, registry); strings(machine);
    const std::uint8_t body[] = "body"; REQUIRE(machine.global.write(0x120, sizeof body, body));
    REQUIRE(machine.spawn({}, "x", "intro")); REQUIRE(machine.run_frame(0));
    for (unsigned step = 1; step <= 3; ++step) { REQUIRE(queue.run_one()); REQUIRE(machine.run_frame(step)); }
    CHECK(word(machine, 0x200) == 9); CHECK(queue.stats().admitted == 0);
    CHECK(mixer.state(Bank::Stream, 2) == ChannelState::Playing);
    std::int16_t output[400]{}; mixer.mix(output, 200);
    CHECK(output[0] > 10000); CHECK(output[398] > 10000); CHECK(mixer.loop_count(Bank::Stream, 2) >= 2);
}

TEST_CASE("real background decoding is off the caller thread and cancellation prevents publication") {
    Files files; files.arc("item", {8}); ArcResourceFinder finder(files.path);
    std::promise<void> started, finish; auto began = started.get_future(); auto gate = finish.get_future().share();
    std::thread::id worker; DecodeQueue queue(1);
    struct Release { std::promise<void>& gate; bool done = false; ~Release() { if (!done) gate.set_value(); } } release{finish};
    Cache cache(finder, [&](OwnedBytes raw) -> Result<Cache::Pointer> {
        worker = std::this_thread::get_id(); started.set_value(); gate.wait();
        return std::make_shared<const Bytes>(raw.bytes.begin(), raw.bytes.end());
    }, byte_size, 16, &queue);
    auto request = cache.request("data", "item"); REQUIRE(request);
    REQUIRE(began.wait_for(std::chrono::seconds(5)) == std::future_status::ready);
    CHECK(worker != std::this_thread::get_id()); CHECK_FALSE(cache.poll(*request));
    request->reset(); finish.set_value(); release.done = true;
    CHECK(cache.count() == 0);
}

TEST_CASE("failed async bitmap decode reports its opcode and preserves the previous image" * doctest::test_suite("gpu")) {
    Files files; Bytes bad(20, 0); std::memcpy(bad.data(), "DSC FORMAT 1.00", 15); files.arc("item", bad);
    ArcResourceFinder finder(files.path); DecodeQueue queue(0); ArcImageSource images(finder); images.set_queue(&queue);
    RendererOptions options; options.headless = true; auto renderer = Renderer::create(options); REQUIRE(renderer);
    auto display = Display::create(**renderer, 8, 8); REQUIRE(display);
    REQUIRE((*display)->create_slot(7, 1, 1, SlotFormat::Bgra)); REQUIRE((*display)->fill_slot(7, 0xffaabbcc));
    GraphicsHost host{display->get(), &images}; vm::SubopRegistry registry; register_graphics_subops(registry, host);
    Programs programs; programs.set(Code().i(7).i(0x100).i(0x105).op(0x90, 0x10).ret());
    vm::Vm machine(programs, registry); strings(machine); REQUIRE(machine.spawn({}, "x", "broken"));
    REQUIRE(machine.run_frame(0)); REQUIRE(queue.run_one()); auto error = machine.run_frame(1);
    REQUIRE_FALSE(error); CHECK(error.error().message.find("90:10") != std::string::npos);
    CHECK(error.error().message.find("data/item") != std::string::npos);
    auto pixels = (*display)->read_slot(7); REQUIRE(pixels); CHECK((*pixels)[0] == 0xffaabbcc);
}

TEST_CASE("stopping a decode queue completes outstanding futures with an error") {
    Files files; files.arc("item", {1}); ArcResourceFinder finder(files.path); unsigned calls = 0;
    Cache::Request request;
    {
        DecodeQueue queue(0); Cache cache(finder, decoder(calls), byte_size, 16, &queue);
        auto pending = cache.request("data", "item"); REQUIRE(pending); request = *pending;
    }
    CHECK(calls == 0);
    REQUIRE(request->task->future.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    CHECK_FALSE(request->task->future.get());
}

TEST_CASE("async loose overrides refresh and removing them restores the archive cache") {
    Files files; files.arc("item", {1}); ArcResourceFinder finder(files.path); DecodeQueue queue(0);
    unsigned calls = 0; Cache cache(finder, decoder(calls), byte_size, 16, &queue);
    auto load = [&] {
        auto request = cache.request("data", "item"); REQUIRE(request);
        auto ready = cache.poll(*request);
        if (!ready) { REQUIRE(queue.run_one()); ready = cache.poll(*request); }
        REQUIRE(ready); REQUIRE(*ready); return ***ready;
    };
    CHECK(load() == Bytes{1}); CHECK(calls == 1);
    files.write("item", {2}); CHECK(load() == Bytes{2});
    files.write("item", {3}); CHECK(load() == Bytes{3}); CHECK(cache.count() == 1);
    std::filesystem::remove(files.path / "item"); CHECK(load() == Bytes{1}); CHECK(calls == 3);
}
