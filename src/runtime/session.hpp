#pragma once
#include <filesystem>
#include <memory>
#include <string>
#include "core/error.hpp"
#include "vm/vm.hpp"

namespace obgi {
class Renderer;
class Display;
class Mixer;
struct SystemState;

struct SessionOptions {
    std::filesystem::path game_dir, data_dir;
    std::string driver;
    std::uint32_t width = 1280, height = 720;
    bool window = false, audio_output = true;
    bool render_offscreen = false; // exercise presentation in deterministic headless replays
    // Offline runs execute jobs inline: worker completion timing cannot change a replay.
    bool deterministic = true;
    std::int64_t calendar_epoch = 1609459200; // fixed UTC epoch for replayed calendar-time queries
};

// Owns the complete runtime graph and its destruction order. Tools and interactive play
// use the same scheduler, services and input boundary.
class EngineSession {
  public:
    static Result<std::unique_ptr<EngineSession>> create(SessionOptions options);
    ~EngineSession();
    Result<void> boot(std::string_view arc = "autoload.arc", std::string_view name = "ipl._bp");
    Result<void> restart(std::string_view arc = "autoload.arc", std::string_view name = "ipl._bp");
    bool pump_events();
    Result<vm::FrameStatus> tick(std::int64_t now_ms);
    Result<void> present();
    Result<std::vector<std::uint32_t>> capture();
    vm::Vm& machine();
    vm::SubopRegistry& registry();
    SystemState& system();
    Renderer& renderer();
    Display& display();
    Mixer& mixer();
  private:
    EngineSession();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace obgi
