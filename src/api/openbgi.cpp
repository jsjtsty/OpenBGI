#include "openbgi/openbgi.h"

#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "bindings/system_subops.hpp"
#include "runtime/session.hpp"
#include "vm/vm.hpp"

namespace {

namespace fs = std::filesystem;

void copy_error(const std::string& msg, char* out, size_t size) {
    if (!out || size == 0) return;
    std::snprintf(out, size, "%s", msg.c_str());
}

fs::path path_from_utf8(std::string_view s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

bool parse_long(const char* text, long& value) {
    if (!text) return false;
    const char* end = text + std::strlen(text);
    auto result = std::from_chars(text, end, value);
    return result.ec == std::errc{} && result.ptr == end && value >= 0;
}

struct SdlQuitGuard {
    bool active = false;
    ~SdlQuitGuard() {
        if (active) SDL_Quit();
    }
};

} // namespace

extern "C" {

int obgi_run(int argc, char** argv, char* error, size_t error_size) {
    using namespace obgi;
    copy_error("", error, error_size);

    std::string game, driver_s, entry_arc = "autoload.arc", entry_name = "ipl._bp";
    std::string data_dir, resources_dir, shader_dir, log_file;
    std::vector<std::string> font_dirs;
    long frames = 20000;
    bool frames_set = false;
    bool window = false, launcher_mode = false;
    bool render_headless = false, async_resources = false, pace_headless = false;

    auto fail_run = [&](const std::string& msg) -> int {
        copy_error(msg, error, error_size);
        if (!log_file.empty()) {
            std::ofstream lf(path_from_utf8(log_file), std::ios::app);
            if (lf) lf << "ERROR: " << msg << "\n";
        }
        if (!launcher_mode) std::fprintf(stderr, "obgi_run: %s\n", msg.c_str());
        return 1;
    };

    for (int i = 1; i < argc; ++i) {
        if (!argv[i]) continue;
        std::string a = argv[i];
        if (a == "--launcher") launcher_mode = true;
        else if (a == "--window") window = true;
        else if (a == "--render-headless") render_headless = true;
        else if (a == "--async-resources") async_resources = true;
        else if (a == "--pace-headless") pace_headless = true;
        else if (a == "--frames" && i + 1 < argc) {
            if (!parse_long(argv[++i], frames)) return fail_run("--frames requires a non-negative integer");
            frames_set = true;
        } else if (a == "--data-dir" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--resources-dir" && i + 1 < argc) resources_dir = argv[++i];
        else if (a == "--shader-dir" && i + 1 < argc) shader_dir = argv[++i];
        else if (a == "--font-dir" && i + 1 < argc) font_dirs.emplace_back(argv[++i]);
        else if (a == "--log-file" && i + 1 < argc) log_file = argv[++i];
        else if (a == "--driver" && i + 1 < argc) driver_s = argv[++i];
        else if (a == "--entry" && i + 2 < argc) {
            entry_arc = argv[++i];
            entry_name = argv[++i];
        } else if (a == "--game-dir" && i + 1 < argc) game = argv[++i];
        else if (!a.empty() && a[0] == '-') return fail_run("unknown or incomplete option: " + a);
        else if (game.empty()) game = a;
        else return fail_run("unexpected argument: " + a);
    }

    if (launcher_mode && !render_headless && !frames_set) window = true;
    if (window && !frames_set) frames = 0x7FFFFFFF;
    if (game.empty()) return fail_run("missing game directory");

    SDL_SetMainReady();
    SdlQuitGuard sdl_guard{true};

    SessionOptions options;
    options.game_dir = path_from_utf8(game);
    if (!data_dir.empty()) options.data_dir = path_from_utf8(data_dir);
    if (!resources_dir.empty()) options.resources_dir = path_from_utf8(resources_dir);
    if (!shader_dir.empty()) options.shader_dir = path_from_utf8(shader_dir);
    for (const auto& fd : font_dirs) options.font_dirs.push_back(path_from_utf8(fd));
    options.window = window;
    options.render_offscreen = render_headless;
    options.async_resources = async_resources;
    options.deterministic = !window;
    options.driver = driver_s;

    auto created = EngineSession::create(std::move(options));
    if (!created) return fail_run(created.error().message);
    std::unique_ptr<EngineSession> session = std::move(*created);

    if (auto boot = session->boot(entry_arc, entry_name); !boot) {
        std::string msg = boot.error().message;
        session.reset();
        return fail_run(msg);
    }

    auto& vm = session->machine();
    auto& system = session->system();
    const auto started = std::chrono::steady_clock::now();

    for (long f = 0; f < frames; ++f) {
        if (!session->pump_events()) break;

        const auto now_ms = window
            ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
            : f * std::int64_t{16};
        auto st = session->tick(now_ms);
        if (st) {
            if (auto shown = session->present(); !shown) {
                std::string msg = shown.error().message;
                session.reset();
                return fail_run(msg);
            }
        }
        if (!st) {
            std::string msg = st.error().message;
            session.reset();
            return fail_run(msg);
        }
        if (system.quit_requested || *st == vm::FrameStatus::AllThreadsDead) break;

        if (window || pace_headless) {
            const auto deadline = window ? started + std::chrono::milliseconds(vm.now_ms + std::max(system.frame_ms, 1u))
                                         : started + std::chrono::milliseconds((f + 1) * 16);
            while (std::chrono::steady_clock::now() < deadline) {
                if (session->wait_for_resources(deadline)) {
                    const auto wake_ms = window ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
                                                : now_ms;
                    auto resumed = session->resume_resources(wake_ms);
                    if (!resumed) {
                        std::string msg = resumed.error().message;
                        session.reset();
                        return fail_run(msg);
                    }
                    if (*resumed == vm::FrameStatus::AllThreadsDead || system.quit_requested) break;
                    if (auto shown = session->present(); !shown) {
                        std::string msg = shown.error().message;
                        session.reset();
                        return fail_run(msg);
                    }
                } else if (window && std::chrono::steady_clock::now() < deadline) {
                    if (!session->pump_events()) break;
                    if (std::any_of(system.input_pending.begin(), system.input_pending.end(), [](auto v) { return v != 0; }) ||
                        !system.wheel_events.empty())
                        break;
                }
            }
        } else if (async_resources && session->wait_for_resources(std::chrono::steady_clock::now())) {
            if (auto resumed = session->resume_resources(now_ms); !resumed) {
                std::string msg = resumed.error().message;
                session.reset();
                return fail_run(msg);
            }
        }
    }

    session.reset();
    return 0;
}

} // extern "C"
