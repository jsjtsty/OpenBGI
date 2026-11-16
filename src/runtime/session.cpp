#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_keycode.h>
#include "text/encoding.hpp"
#include "runtime/session.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <string_view>
#include <thread>
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
#include "resources/translation_detection.hpp"
#include "bindings/eustia_auto.hpp"
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
    if (!std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isalnum(ch) != 0 || ch == '_'; }))
        return std::nullopt;
    return std::string(name);
}

std::size_t edit_box_units(std::u32string_view s) {
    std::size_t n = 0;
    for (char32_t cp : s) {
        n += (cp < 0x80 || (cp >= 0xFF61 && cp <= 0xFF9F)) ? 1 : 2;
    }
    return n;
}

void render_edit_box(SystemState& system, Display& display, FontHost& font_host, std::int64_t now_ms, int& last_caret) {
    constexpr std::uint32_t kEditSprite = 0x7F900000u;
    auto& eb = system.edit_box;
    if (!eb.active || !eb.visible || eb.w < 8 || eb.h < 8) {
        display.remove_sprite(kEditSprite);
        last_caret = -1;
        return;
    }
    const int caret_phase = eb.focused ? static_cast<int>((std::max<std::int64_t>(0, now_ms) / 500) & 1) : 1;
    if (!eb.dirty && caret_phase == last_caret && display.find_sprite(kEditSprite)) return;
    eb.dirty = false;
    last_caret = caret_phase;

    const std::uint32_t w = static_cast<std::uint32_t>(eb.w);
    const std::uint32_t h = static_cast<std::uint32_t>(eb.h);
    std::vector<std::uint32_t> px(std::size_t{w} * h, 0xFFFFFFFFu);
    const std::uint32_t border = eb.focused ? 0xFF4A90E2u : 0xFF888888u;
    for (std::uint32_t x = 0; x < w; ++x) {
        px[x] = border;
        px[std::size_t{h - 1} * w + x] = border;
    }
    for (std::uint32_t y = 0; y < h; ++y) {
        px[std::size_t{y} * w] = border;
        px[std::size_t{y} * w + (w - 1)] = border;
    }

    std::shared_ptr<Font> face;
    if (font_host.fonts && !font_host.table.ids.empty()) {
        const std::size_t idx = eb.font_index < font_host.table.ids.size() ? eb.font_index : 0;
        const std::string& fname = font_host.table.ids[idx];
        const int charset = idx < font_host.table.infos.size() ? font_host.table.infos[idx].charset : -1;
        if (auto f = font_host.fonts->open(fname, static_cast<int>(eb.font_size), charset)) {
            face = std::move(*f);
        }
    }

    const std::u32string editing_u32 = utf8_to_utf32(eb.editing);
    std::u32string full = eb.text;
    full += editing_u32;

    int total_adv = 0;
    int committed_adv = 0;
    if (face) {
        for (std::size_t i = 0; i < full.size(); ++i) {
            if (const Glyph* g = face->glyph(full[i])) total_adv += g->advance;
            if (i + 1 == eb.text.size()) committed_adv = total_adv;
        }
    }

    constexpr int kPad = 4;
    const int avail = std::max(1, eb.w - 2 * kPad);
    int pen_x = kPad;
    if (total_adv <= avail) {
        if (eb.align == 1) pen_x = kPad + (avail - total_adv) / 2;
        else if (eb.align == 2) pen_x = eb.w - kPad - total_adv;
    } else {
        pen_x = kPad + avail - total_adv;
    }

    const std::uint32_t fg_r = (eb.text_color >> 16) & 0xFFu;
    const std::uint32_t fg_g = (eb.text_color >> 8) & 0xFFu;
    const std::uint32_t fg_b = eb.text_color & 0xFFu;

    if (face) {
        const int baseline = (eb.h + face->ascent() - face->descent()) / 2;
        int cur_x = pen_x;
        for (std::size_t i = 0; i < full.size(); ++i) {
            const Glyph* g = face->glyph(full[i]);
            if (!g) continue;
            const int gx0 = cur_x + g->left;
            const int gy0 = baseline - g->top;
            for (int row = 0; row < g->height; ++row) {
                const int py = gy0 + row;
                if (py < 1 || py >= eb.h - 1) continue;
                for (int col = 0; col < g->width; ++col) {
                    const int px_x = gx0 + col;
                    if (px_x < 1 || px_x >= eb.w - 1) continue;
                    const std::uint32_t cov = g->coverage[std::size_t(row) * g->width + col];
                    if (!cov) continue;
                    std::uint32_t& dst = px[std::size_t(py) * w + px_x];
                    const std::uint32_t db = dst & 0xFFu, dg = (dst >> 8) & 0xFFu, dr = (dst >> 16) & 0xFFu;
                    const std::uint32_t inv = 255u - cov;
                    const std::uint32_t ob = (fg_b * cov + db * inv + 127u) / 255u;
                    const std::uint32_t og = (fg_g * cov + dg * inv + 127u) / 255u;
                    const std::uint32_t or_ = (fg_r * cov + dr * inv + 127u) / 255u;
                    dst = 0xFF000000u | (or_ << 16) | (og << 8) | ob;
                }
            }
            if (i >= eb.text.size() && baseline + 2 < eb.h - 1) {
                for (int cx = std::max(1, cur_x); cx < std::min(eb.w - 1, cur_x + g->advance); ++cx) {
                    px[std::size_t(baseline + 2) * w + cx] = border;
                }
            }
            cur_x += g->advance;
        }
    }

    if (eb.focused && caret_phase == 0) {
        const int caret_x = std::clamp(pen_x + committed_adv, 2, eb.w - 3);
        const int cy0 = std::max(3, (eb.h - static_cast<int>(eb.font_size)) / 2);
        const int cy1 = std::min(eb.h - 3, cy0 + static_cast<int>(eb.font_size));
        for (int cy = cy0; cy < cy1; ++cy) {
            px[std::size_t(cy) * w + caret_x] = 0xFF000000u | eb.text_color;
        }
    }

    (void)display.upload_slot(kEditSprite, w, h, SlotFormat::Bgra, px);
    Sprite& sp = display.sprite(kEditSprite);
    sp.slot = kEditSprite;
    sp.x = eb.x;
    sp.y = eb.y;
    sp.priority = 0x7FFFFFF0u;
    sp.blend = 1;
    sp.alpha = 0;
    sp.visible = true;
}

} // namespace

struct EngineSession::Impl {
    SessionOptions options;
    std::unique_ptr<Renderer> renderer;
    std::unique_ptr<Display> display;
    std::unique_ptr<FontLibrary> fonts;
    ArcResourceFinder resources;
    ArcProgramProvider programs;
    std::unique_ptr<DecodeQueue> resource_jobs;
    std::uint64_t resource_generation = 0;
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
    int last_edit_caret = -1;

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
    if (!m.options.deterministic || m.options.async_resources) {
        m.resource_jobs = std::make_unique<DecodeQueue>();
        m.images.set_queue(m.resource_jobs.get());
    }
    if (auto name = game_name_from_system_ipl(m.programs)) m.system.game_name = std::move(*name);
    // Eustia uses point channels 24..31 for spoken dialogue and sets AUTO delay
    // to zero during voice playback; retain voice until it ends.
    if (m.system.game_name == "AiyokunoEustia_Product") {
        m.system.auto_voice_active = [&m]() {
            for (std::uint32_t channel = 24; channel < 32; ++channel)
                if (m.mixer.state(Bank::Point, channel) == ChannelState::Playing) return true;
            return false;
        };
        m.system.auto_voice_delay = [&m]() -> std::optional<std::uint32_t> {
            std::uint8_t setting = 0;
            if (!m.vm || !m.vm->global_ready || !m.vm->global.read(0x8a, 1, &setting)) return std::nullopt;
            return eustia_auto_delay(setting);
        };
    }
    if (has_gbk_story_patch(m.resources)) {
        m.system.code_page = 936;
        m.system.code_page_locked = true;
    }
    RendererOptions ro;
    ro.headless = !m.options.window;
    ro.logical_width = m.options.width; ro.logical_height = m.options.height;
    ro.driver = m.options.driver.empty() ? nullptr : m.options.driver.c_str();
    ro.shader_dir = !m.options.shader_dir.empty()
        ? m.options.shader_dir
        : (!m.options.resources_dir.empty() ? m.options.resources_dir / "shaders" : std::filesystem::path{});
    auto renderer = Renderer::create(ro);
    if (!renderer) return std::unexpected(renderer.error());
    m.renderer = std::move(*renderer);
    if (m.options.window && m.resource_jobs) m.resource_jobs->set_completion_notifier(m.renderer->completion_notifier());
    auto display = Display::create(*m.renderer, m.options.width, m.options.height);
    if (!display) return std::unexpected(display.error());
    m.display = std::move(*display);
    auto fonts = FontLibrary::create();
    if (!fonts) return std::unexpected(fonts.error());
    for (const auto& dir : m.options.font_dirs) (*fonts)->add_search_dir(dir);
    if (!m.options.resources_dir.empty()) (*fonts)->add_search_dir(m.options.resources_dir / "fonts");
    if (const char* base = SDL_GetBasePath()) (*fonts)->add_search_dir(std::filesystem::path(base) / "fonts");
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
    m.sound = {&m.mixer, &m.resources, m.resource_jobs.get()};
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
    // The BGI animation manager yields after polling up to 288 animation slots.
    // Its 1 kHz polling is redundant between presented frames; other threads
    // retain their script-selected scheduler rate and message wakeups.
    m.vm->yield_interval = [&m](const vm::Thread& thread) -> std::uint32_t {
        if (thread.modules.empty() || thread.modules.front().arc != "sysprg.arc" ||
            thread.modules.front().name != "anmmngrs._bp") return 0;
        return std::max(std::max(1u, m.system.frame_ms) * 1000u, m.system.presentation_interval_us);
    };
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
    // Launchers pass the historical default explicitly; choose the legacy entry only
    // when that archive is absent, leaving explicit alternative entries untouched.
    if (ascii_lower(arc) == "autoload.arc" && ascii_lower(name) == "ipl._bp") {
        bool has_autoload = false;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(
                 impl_->options.game_dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ascii_lower(entry.path().filename().string()) == "autoload.arc" && entry.is_regular_file(ec)) {
                has_autoload = true;
                break;
            }
        }
        if (!has_autoload && !ec) arc = "system.arc";
    }
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
        if (m.system.edit_box.active && m.system.edit_box.visible && press.pointer && press.key == 1) {
            const auto& eb = m.system.edit_box;
            const bool inside = press.x >= eb.x && press.x < eb.x + eb.w && press.y >= eb.y && press.y < eb.y + eb.h;
            if (inside != m.system.edit_box.focused) {
                m.system.edit_box.focused = inside;
                m.system.edit_box.dirty = true;
            }
            if (inside) continue;
        }
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
    if (m.system.edit_box.active && m.system.edit_box.visible && m.system.edit_box.focused) {
        auto& eb = m.system.edit_box;
        m.renderer->set_text_input(true, eb.x, eb.y, eb.w, eb.h);
        for (const std::string& chunk : m.renderer->take_text_inputs()) {
            for (char32_t cp : utf8_to_utf32(chunk)) {
                if (cp < 0x20 || cp == 0x7F) continue;
                if (eb.no_ascii && cp >= 0x20 && cp <= 0x7E) continue;
                const std::size_t add_u = (cp < 0x80 || (cp >= 0xFF61 && cp <= 0xFF9F)) ? 1 : 2;
                if (edit_box_units(eb.text) + add_u <= eb.max_chars) {
                    eb.text.push_back(cp);
                    eb.dirty = true;
                }
            }
        }
        const std::string cur_edit = m.renderer->text_editing();
        if (cur_edit != eb.editing) {
            eb.editing = cur_edit;
            eb.dirty = true;
        }
        for (std::uint32_t k : m.renderer->take_edit_keys()) {
            if (k == SDLK_BACKSPACE) {
                if (eb.editing.empty() && !eb.text.empty()) {
                    eb.text.pop_back();
                    eb.dirty = true;
                }
            } else if (k == SDLK_RETURN || k == SDLK_KP_ENTER) {
                eb.focused = false;
                if (eb.hide_on_enter) eb.visible = false;
                eb.dirty = true;
            } else if (k == SDLK_ESCAPE) {
                eb.focused = false;
                eb.dirty = true;
            }
        }
        if (!eb.focused || !eb.visible) m.renderer->set_text_input(false);
    } else {
        m.renderer->set_text_input(false);
    }
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
    // Drain events at frame boundaries as well: a continuously busy renderer
    // may never enter the host's blocking resource wait.
    if (m.resource_jobs && m.resource_jobs->take_completion(m.resource_generation)) m.vm->notify_resource_wait();
    auto status = m.vm->run_frame(now);
    if (m.system.shutdown_message_thread && !m.vm->find(*m.system.shutdown_message_thread))
        m.system.quit_requested = true;
    m.display->resolve_masters();
    // Scripts may change window visibility after the frame hook ran.
    panel_sync(m.system);
    return status;
}

bool EngineSession::wait_for_resources(std::chrono::steady_clock::time_point deadline) {
    auto& m = *impl_;
    if (m.resource_jobs && m.resource_jobs->take_completion(m.resource_generation)) return true;
    if (m.options.window) {
        // If an SDL event is filtered or the queue is full, periodically recheck
        // the generation so a completed decode cannot remain stranded.
        m.renderer->wait_for_event(std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(16)));
        return m.resource_jobs && m.resource_jobs->take_completion(m.resource_generation);
    }
    if (m.resource_jobs) return m.resource_jobs->wait_for_completion(m.resource_generation, deadline);
    std::this_thread::sleep_until(deadline);
    return false;
}

Result<vm::FrameStatus> EngineSession::resume_resources(std::int64_t now) {
    auto& m = *impl_;
    if (now < m.last_ms) return fail(Errc::Range, "session clock moved backwards");
    m.last_ms = now;
    auto status = m.vm->resources_ready(now);
    m.display->resolve_masters();
    panel_sync(m.system);
    return status;
}

Result<void> EngineSession::present() {
    auto& m = *impl_;
    if ((!m.options.window && !m.options.render_offscreen) || (!m.player->active() && !m.system.display_enabled)) return {};
    m.system.presentation_interval_us = m.options.window ? m.renderer->presentation_interval_us() : 1000u;
    const auto interval = std::max(std::max(1u, m.system.frame_ms) * 1000u, m.system.presentation_interval_us);
    if (interval != m.present_interval) { m.present_interval = interval; m.next_present = 0; }
    const auto now_us = m.vm->now_ms * 1000;
    if (now_us < m.next_present) return {};
    // Accumulate fractional refresh periods rather than rounding every frame to
    // milliseconds; after a long stall, drop missed frames instead of catching up.
    if (!m.next_present || now_us - m.next_present >= interval) m.next_present = now_us + interval;
    else m.next_present += interval;
    if (!m.player->active()) render_edit_box(m.system, *m.display, m.font, m.last_ms, m.last_edit_caret);
    return m.player->active() ? m.player->present() : m.display->present();
}

Result<std::vector<std::uint32_t>> EngineSession::capture() {
    auto& m = *impl_;
    if (!m.player->active()) {
        render_edit_box(m.system, *m.display, m.font, m.last_ms, m.last_edit_caret);
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
