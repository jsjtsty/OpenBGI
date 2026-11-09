#include "runtime/session.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string_view>
#include "audio/device.hpp"
#include "audio/timeline.hpp"
#include "bindings/arc_image_source.hpp"
#include "bindings/arc_program_provider.hpp"
#include "bindings/audio_subops.hpp"
#include "bindings/bitmap_effect_subops.hpp"
#include "bindings/font_subops.hpp"
#include "bindings/graphics_subops.hpp"
#include "bindings/message_subops.hpp"
#include "bindings/movie_subops.hpp"
#include "bindings/panel.hpp"
#include "bindings/particle_subops.hpp"
#include "bindings/sequence_subops.hpp"
#include "bindings/system_subops.hpp"

namespace obgi {

namespace {
std::filesystem::path default_data_dir(const std::filesystem::path& game) {
    std::filesystem::path base;
#ifdef _WIN32
    if (const auto local = std::getenv("LOCALAPPDATA")) base = local;
#else
    if (const auto xdg = std::getenv("XDG_DATA_HOME")) base = xdg;
    else if (const auto home = std::getenv("HOME")) base = std::filesystem::path(home) / ".local/share";
#endif
    if (base.empty()) base = std::filesystem::temp_directory_path();
    std::uint64_t hash = 14695981039346656037ull;
    for (auto ch : game.generic_u8string()) hash = (hash ^ static_cast<unsigned char>(ch)) * 1099511628211ull;
    std::ostringstream id;
    id << std::hex << hash;
    return base / "OpenBGI" / id.str();
}

std::optional<std::string> game_name_from_system_ipl(vm::ProgramProvider& programs) {
    auto block = programs.load("system.arc", "ipl._bp");
    if (!block || block->size() < 8) return std::nullopt;
    const std::uint32_t offset = load_u32le(block->data());
    const std::uint32_t size = load_u32le(block->data() + 4);
    if (static_cast<std::uint64_t>(offset) + size > block->size()) return std::nullopt;
    const std::string_view code(reinterpret_cast<const char*>(block->data() + offset), size);
    constexpr std::string_view marker("debug.inf\0", 10);
    const auto marker_at = code.find(marker);
    if (marker_at == std::string_view::npos) return std::nullopt;
    const std::size_t begin = marker_at + marker.size();
    const auto end = code.find('\0', begin);
    if (end == std::string_view::npos || end == begin || end - begin > 128) return std::nullopt;
    const std::string_view name = code.substr(begin, end - begin);
    if (!std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isalnum(ch) != 0; }))
        return std::nullopt;
    return std::string(name);
}
} // namespace

struct EngineSession::Impl {
    SessionOptions options;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Display> display;
    std::unique_ptr<FontLibrary> fonts;
    ArcResourceFinder resources;
    ArcProgramProvider programs;
    ArcImageSource images;
    Mixer mixer;
    AudioTimeline audio_time{mixer};
    SystemState system;
    InputWaitPort input{system};
    GraphicsHost graphics;
    TextHost text;
    SoundHost sound;
    FontHost font;
    MovieHost movie;
    std::unique_ptr<SequenceService> sequences;
    std::unique_ptr<MoviePlayer> player;
    std::unique_ptr<AudioDevice> device;
    vm::SubopRegistry registry;
    std::unique_ptr<vm::Vm> vm;
    std::int64_t last_ms = -1, next_present = 0;
    std::uint32_t present_interval = 0;

    explicit Impl(SessionOptions opt) : options(std::move(opt)), resources(options.game_dir), programs(resources), images(resources) {}
};

EngineSession::EngineSession() = default;
EngineSession::~EngineSession() = default;

Result<std::unique_ptr<EngineSession>> EngineSession::create(SessionOptions options) {
    std::error_code ec;
    options.game_dir = std::filesystem::weakly_canonical(options.game_dir, ec);
    if (ec || !std::filesystem::is_directory(options.game_dir)) return fail(Errc::Io, "game directory does not exist");
    if (options.data_dir.empty()) options.data_dir = default_data_dir(options.game_dir);
    std::filesystem::create_directories(options.data_dir, ec);
    if (ec) return fail(Errc::Io, "cannot create data directory: " + ec.message());
    std::unique_ptr<EngineSession> session(new EngineSession);
    session->impl_ = std::make_unique<Impl>(std::move(options));
    auto& m = *session->impl_;
    if (auto name = game_name_from_system_ipl(m.programs)) m.system.game_name = std::move(*name);
    RendererOptions ro;
    ro.headless = !m.options.window;
    ro.logical_width = m.options.width; ro.logical_height = m.options.height;
    ro.driver = m.options.driver.empty() ? nullptr : m.options.driver.c_str();
    auto renderer = Renderer::create(ro);
    if (!renderer) return std::unexpected(renderer.error());
    m.renderer = std::move(*renderer);
    auto display = Display::create(*m.renderer, m.options.width, m.options.height);
    if (!display) return std::unexpected(display.error());
    m.display = std::move(*display);
    auto fonts = FontLibrary::create();
    if (!fonts) return std::unexpected(fonts.error());
    m.fonts = std::move(*fonts);
    m.system.game_dir = m.options.game_dir;
    m.system.data_dir = m.options.data_dir;
    m.system.game_path = m.options.game_dir.string() + "\\";
    m.system.message_box = [](const std::string& text) { std::fprintf(stderr, "message box: %s\n", text.c_str()); };
    m.system.confirm_box = [](const std::string& text, bool, bool default_yes) {
        std::fprintf(stderr, "question box: %s -> %s\n", text.c_str(), default_yes ? "yes" : "no");
        return default_yes;
    };
    m.graphics = {m.display.get(), &m.images};
    m.text.display = m.display.get(); m.text.fonts = m.fonts.get(); m.text.system = &m.system;
    m.text.code_page = &m.system.code_page; m.text.font_ids = &m.font.table.ids;
    m.font.fonts = m.fonts.get();
    m.sound = {&m.mixer, &m.resources};
    m.movie.resources = &m.resources;
    m.movie.realtime = m.options.window;
    m.sequences = std::make_unique<SequenceService>(*m.display, m.options.deterministic);
    m.player = std::make_unique<MoviePlayer>(*m.renderer, m.mixer);
    m.movie.player = m.player.get();
    vm::register_core_subops(m.registry);
    register_graphics_subops(m.registry, m.graphics);
    register_text_subops(m.registry, m.text);
    register_font_subops(m.registry, m.font, &m.resources);
    register_audio_subops(m.registry, m.sound);
    register_movie_subops(m.registry, m.movie);
    register_system_subops(m.registry, m.system, m.resources);
    register_window_subops(m.registry, m.system, m.display.get());
    register_table_subops(m.registry, m.system);
    register_knob_subops(m.registry, m.system);
    register_sequence_subops(m.registry, *m.sequences, m.resources);
    register_bitmap_effect_subops(m.registry, m.display.get());
    register_particle_subops(m.registry, m.system, m.display.get());
    register_plane_subops(m.registry, m.system, m.display.get());
    register_message_subops(m.registry, m.system, m.text, m.font);
    m.vm = std::make_unique<vm::Vm>(m.programs, m.registry);
    m.vm->global_ready = false;
    m.vm->wait_input = &m.input;
    if (m.options.deterministic) m.system.calendar_time = [&m] {
        const auto time = static_cast<std::time_t>(m.options.calendar_epoch + m.vm->now_ms / 1000);
        std::tm calendar{};
#ifdef _WIN32
        gmtime_s(&calendar, &time);
#else
        gmtime_r(&time, &calendar);
#endif
        return std::array<std::uint16_t, 8>{std::uint16_t(calendar.tm_year + 1900), std::uint16_t(calendar.tm_mon + 1),
            std::uint16_t(calendar.tm_wday), std::uint16_t(calendar.tm_mday), std::uint16_t(calendar.tm_hour),
            std::uint16_t(calendar.tm_min), std::uint16_t(calendar.tm_sec), std::uint16_t(m.vm->now_ms % 1000)};
    };
    m.vm->frame_hook = [&m](std::int64_t now) {
        m.display->resolve_masters();
        advance_motions(m.system, *m.display, now);
    };
    if (m.options.window && m.options.audio_output) {
        auto device = AudioDevice::open(m.mixer);
        if (device) m.device = std::move(*device);
        else std::fprintf(stderr, "audio output unavailable; using silent playback: %s\n", device.error().message.c_str());
    }
    if (m.options.window) m.renderer->raise_window();
    return session;
}

Result<void> EngineSession::boot(std::string_view arc, std::string_view name) {
    if (!impl_->vm->threads().empty()) return fail(Errc::Range, "session is already running");
    auto spawned = impl_->vm->spawn(vm::ThreadConfig{}, arc, name);
    if (!spawned) return std::unexpected(spawned.error());
    return {};
}

Result<void> EngineSession::restart(std::string_view arc, std::string_view name) {
    auto replacement = create(impl_->options);
    if (!replacement) return std::unexpected(replacement.error());
    if (auto booted = (*replacement)->boot(arc, name); !booted) return booted;
    impl_.swap((*replacement)->impl_);
    return {};
}

bool EngineSession::pump_events() {
    auto& m = *impl_;
    if (!m.options.window) return !m.system.quit_requested;
    if (!m.renderer->poll_events()) { m.system.quit_requested = true; m.player->stop(MovieEnd::WindowClosed); return false; }
    for (auto press : m.renderer->take_presses()) {
        m.system.press_input(press.key);
        if (press.pointer && (press.key == 1 || (press.key == 2 && m.system.swap_mouse_buttons))) {
            m.system.click_x = press.x; m.system.click_y = press.y; m.system.click_position_valid = true;
        }
    }
    m.system.set_input_held(m.renderer->held_inputs());
    std::tie(m.system.mouse_x, m.system.mouse_y) = m.renderer->mouse_position();
    m.system.left_down = m.system.input_held[1];
    for (auto step : m.renderer->take_wheel_events()) { m.system.wheel_events.push_back(step); ++m.system.input_serial; }
    (void)m.renderer->take_wheel();
    if (m.renderer->take_key() == 27 && m.player->active()) m.player->stop(MovieEnd::Skipped);
    return !m.system.quit_requested;
}

Result<vm::FrameStatus> EngineSession::tick(std::int64_t now) {
    auto& m = *impl_;
    if (now < 0 || now < m.last_ms) return fail(Errc::Range, "session clock moved backwards");
    m.last_ms = now;
    if (!m.options.deterministic) {
        const auto frame_start = std::chrono::steady_clock::now();
        m.system.frame_elapsed_ms = [frame_start] {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frame_start).count();
        };
    }
    if (!m.device) m.audio_time.advance(now);
    if (auto movie = m.player->update(now); !movie) return std::unexpected(movie.error());
    auto status = m.vm->run_frame(now);
    if (m.system.shutdown_message_thread && !m.vm->find(*m.system.shutdown_message_thread))
        m.system.quit_requested = true;
    m.display->resolve_masters();
    // Scripts may change window visibility after the frame hook ran.
    panel_sync(m.system);
    return status;
}

Result<void> EngineSession::present() {
    auto& m = *impl_;
    if ((!m.options.window && !m.options.render_offscreen) || (!m.player->active() && !m.system.display_enabled)) return {};
    const auto interval = std::max(1u, m.system.frame_ms);
    if (interval != m.present_interval) { m.present_interval = interval; m.next_present = 0; }
    if (m.vm->now_ms < m.next_present) return {};
    m.next_present = m.vm->now_ms + interval;
    return m.player->active() ? m.player->present() : m.display->present();
}

Result<std::vector<std::uint32_t>> EngineSession::capture() {
    auto& m = *impl_;
    if (!m.player->active()) {
        if (auto composed = m.display->compose(); !composed) return std::unexpected(composed.error());
        return m.display->read_screen();
    }
    if (m.options.window) return fail(Errc::Unsupported, "movie capture requires an offscreen session");
    if (auto shown = m.player->present(); !shown) return std::unexpected(shown.error());
    auto bytes = m.renderer->read_back();
    if (!bytes) return std::unexpected(bytes.error());
    std::vector<std::uint32_t> pixels(bytes->size() / 4);
    for (std::size_t i = 0; i < pixels.size(); ++i)
        pixels[i] = (*bytes)[4 * i + 2] | (std::uint32_t{(*bytes)[4 * i + 1]} << 8) |
                    (std::uint32_t{(*bytes)[4 * i]} << 16) | (std::uint32_t{(*bytes)[4 * i + 3]} << 24);
    return pixels;
}

vm::Vm& EngineSession::machine() { return *impl_->vm; }
vm::SubopRegistry& EngineSession::registry() { return impl_->registry; }
SystemState& EngineSession::system() { return impl_->system; }
Renderer& EngineSession::renderer() { return *impl_->renderer; }
Display& EngineSession::display() { return *impl_->display; }
Mixer& EngineSession::mixer() { return impl_->mixer; }

} // namespace obgi
