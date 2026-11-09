#include <chrono>
#include <cstdlib>
#include <cstdio>
#include "render/display.hpp"

#include "render/background_fx.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <cstring>
#include <string>

namespace obgi {

namespace {

Error sdl_error(const char* what) { return Error{Errc::Io, std::string(what) + ": " + SDL_GetError()}; }

struct BlitParams {
    std::uint32_t sizes[4]; // dst w, dst h, src w, src h
    std::int32_t origin[4]; // dst x, dst y, src x / fill value, src y
    std::uint32_t job[4];   // rect w, rect h, mode | sf << 8 | df << 10 | P << 16, fill flag
};
struct PresentParams {
    std::uint32_t info[4]; // width, height, format
};

struct GpuSlot {
    SlotInfo info;
    SDL_GPUBuffer* buffer = nullptr;
    std::uint64_t generation = 0;
};

} // namespace

bool blend_mode_supported(std::uint32_t mode) {
    // The modes whose kernels are ported into shaders/blend.glsl (see analysis/混合公式.md).
    return mode <= 9 || (mode >= 0x20 && mode <= 0x27) || mode == 0xC0 || mode == 0x80;
}

struct Display::Impl {
    Renderer* renderer = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUComputePipeline* blit_pipeline = nullptr;
    SDL_GPUComputePipeline* present_pipeline = nullptr;
    SDL_GPUComputePipeline* crossfade_pipeline = nullptr;
    SDL_GPUComputePipeline* filter_pipeline = nullptr;
    SDL_GPUBuffer* dummy = nullptr; // bound as the unused read-only buffer of fills
    std::map<std::uint32_t, GpuSlot> slots;
    std::uint64_t next_generation = 0;
    // a wipe sprite's picture and gray pattern as pixels, and the progress its temporary slot was last made for
    struct WipeCache {
        std::uint32_t src = 0, gray = 0, progress = 0xFFFFFFFFu, mode = 0;
        std::int32_t x = 0, y = 0; // where the picture was when a screen-aligned map was applied
        bool through_gray = false;
        std::vector<std::uint32_t> src_px, gray_px;
    };
    std::map<std::uint32_t, WipeCache> wipes;
    // a projected plane's picture as pixels, and the scale (16.16) its shrunk copy in a temporary slot was made for
    struct ScaleCache {
        std::uint32_t src = 0;
        std::int32_t scale = 0;
        std::vector<std::uint32_t> src_px;
    };
    std::map<std::uint32_t, ScaleCache> scales;
    // what a sprite's temporary slots (the mix of two pictures, the transformed picture) were last made for, and the anchor of the latter
    struct TempCache {
        std::vector<std::int64_t> key;
        std::int32_t anchor_x = 0, anchor_y = 0;
    };
    std::map<std::uint32_t, TempCache> mixes, xforms;
    // the pictures of the background types as pixels, and what the temporary slot of the rendered background was last made for
    std::map<std::uint32_t, bgfx::Image> bg_images;
    std::map<std::uint32_t, std::uint64_t> versions; // how often each slot's pixels changed
    std::vector<std::int64_t> bg_key;
    // a slot's pixels changed: the pixels cached from it are stale (the temporary slots 0x7F... are the cache's own)
    void stale(std::uint32_t id) {
        if (id >= 0x7F000000u) return;
        bg_images.erase(id);
        ++versions[id];
        for (auto it = wipes.begin(); it != wipes.end();) it = (it->second.src == id || it->second.gray == id) ? wipes.erase(it) : std::next(it);
        for (auto it = scales.begin(); it != scales.end();) it = it->second.src == id ? scales.erase(it) : std::next(it);
    }
    GpuSlot screen;
    std::unique_ptr<Texture> screen_tex;
    SDL_GPUCommandBuffer* cmd = nullptr;

    ~Impl() {
        if (cmd) SDL_CancelGPUCommandBuffer(cmd);
        for (auto& [id, s] : slots) SDL_ReleaseGPUBuffer(device, s.buffer);
        if (screen.buffer) SDL_ReleaseGPUBuffer(device, screen.buffer);
        if (dummy) SDL_ReleaseGPUBuffer(device, dummy);
        if (blit_pipeline) SDL_ReleaseGPUComputePipeline(device, blit_pipeline);
        if (present_pipeline) SDL_ReleaseGPUComputePipeline(device, present_pipeline);
        if (crossfade_pipeline) SDL_ReleaseGPUComputePipeline(device, crossfade_pipeline);
        if (filter_pipeline) SDL_ReleaseGPUComputePipeline(device, filter_pipeline);
        screen_tex.reset();
    }

    Result<SDL_GPUCommandBuffer*> ensure_cmd() {
        if (!cmd) {
            cmd = SDL_AcquireGPUCommandBuffer(device);
            if (!cmd) return std::unexpected(sdl_error("acquire command buffer"));
        }
        return cmd;
    }

    // Submits what has been recorded; with wait, blocks until the GPU is done with it.
    Result<void> flush(bool wait) {
        if (!cmd) return {};
        SDL_GPUCommandBuffer* c = std::exchange(cmd, nullptr);
        if (!wait) {
            if (!SDL_SubmitGPUCommandBuffer(c)) return std::unexpected(sdl_error("submit"));
            return {};
        }
        SDL_GPUFence* f = SDL_SubmitGPUCommandBufferAndAcquireFence(c);
        if (!f) return std::unexpected(sdl_error("submit"));
        SDL_WaitForGPUFences(device, true, &f, 1);
        SDL_ReleaseGPUFence(device, f);
        return {};
    }

    Result<SDL_GPUBuffer*> make_buffer(std::uint32_t pixels) {
        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        bi.size = std::max<std::uint32_t>(pixels, 1) * 4;
        SDL_GPUBuffer* b = SDL_CreateGPUBuffer(device, &bi);
        if (!b) return std::unexpected(sdl_error("slot buffer"));
        return b;
    }

    Result<void> upload(SDL_GPUBuffer* dst, std::span<const std::uint32_t> data) {
        auto c = ensure_cmd();
        if (!c) return std::unexpected(c.error());
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = static_cast<Uint32>(data.size_bytes());
        SDL_GPUTransferBuffer* up = SDL_CreateGPUTransferBuffer(device, &ti);
        if (!up) return std::unexpected(sdl_error("upload buffer"));
        std::memcpy(SDL_MapGPUTransferBuffer(device, up, false), data.data(), data.size_bytes());
        SDL_UnmapGPUTransferBuffer(device, up);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(*c);
        SDL_GPUTransferBufferLocation loc{up, 0};
        SDL_GPUBufferRegion region{dst, 0, static_cast<Uint32>(data.size_bytes())};
        SDL_UploadToGPUBuffer(copy, &loc, &region, false);
        SDL_EndGPUCopyPass(copy);
        SDL_ReleaseGPUTransferBuffer(device, up);
        return {};
    }

    Result<void> dispatch_blit(SDL_GPUBuffer* src, SDL_GPUBuffer* dst, const BlitParams& p) {
        auto c = ensure_cmd();
        if (!c) return std::unexpected(c.error());
        SDL_GPUStorageBufferReadWriteBinding rw{dst, false};
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(*c, nullptr, 0, &rw, 1);
        SDL_BindGPUComputePipeline(pass, blit_pipeline);
        SDL_BindGPUComputeStorageBuffers(pass, 0, &src, 1);
        SDL_PushGPUComputeUniformData(*c, 0, &p, sizeof p);
        SDL_DispatchGPUCompute(pass, (p.job[0] + 7) / 8, (p.job[1] + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
        return {};
    }

    Result<void> crossfade(GpuSlot& dst, GpuSlot& a, GpuSlot& b, std::uint32_t ratio) {
        auto c = ensure_cmd();
        if (!c) return std::unexpected(c.error());
        SDL_GPUStorageBufferReadWriteBinding rw{dst.buffer, false};
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(*c, nullptr, 0, &rw, 1);
        SDL_BindGPUComputePipeline(pass, crossfade_pipeline);
        SDL_GPUBuffer* sources[] = {a.buffer, b.buffer};
        SDL_BindGPUComputeStorageBuffers(pass, 0, sources, 2);
        const std::uint32_t info[] = {a.info.width, a.info.height, ratio, a.info.format == SlotFormat::Bgra ? 1u : 0u};
        SDL_PushGPUComputeUniformData(*c, 0, info, sizeof info);
        SDL_DispatchGPUCompute(pass, (a.info.width + 7) / 8, (a.info.height + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
        return {};
    }

    Result<void> filter(const Sprite& sp, int level, SDL_GPUBuffer* gray) {
        auto c = ensure_cmd();
        if (!c) return std::unexpected(c.error());
        SDL_GPUStorageBufferReadWriteBinding rw{screen.buffer, false};
        auto* pass = SDL_BeginGPUComputePass(*c, nullptr, 0, &rw, 1);
        SDL_BindGPUComputePipeline(pass, filter_pipeline);
        auto* source = gray ? gray : dummy;
        SDL_BindGPUComputeStorageBuffers(pass, 0, &source, 1);
        struct Params {
            std::uint32_t info[4];
            std::int32_t effect[4];
        } params{{screen.info.width, screen.info.height, sp.filter_colour & 0xFFFFFFu,
                  gray ? 4u : static_cast<std::uint32_t>(sp.filter_kind)}, {level, sp.filter_param, 0, 0}};
        SDL_PushGPUComputeUniformData(*c, 0, &params, sizeof params);
        SDL_DispatchGPUCompute(pass, (screen.info.width + 7) / 8, (screen.info.height + 7) / 8, 1);
        SDL_EndGPUComputePass(pass);
        return {};
    }

    Result<void> fill(GpuSlot& s, std::uint32_t value) {
        BlitParams p{};
        p.sizes[0] = s.info.width;
        p.sizes[1] = s.info.height;
        p.origin[2] = static_cast<std::int32_t>(value);
        p.job[0] = s.info.width;
        p.job[1] = s.info.height;
        p.job[3] = 1;
        return dispatch_blit(dummy, s.buffer, p);
    }

    Result<void> do_blit(GpuSlot& dst, std::int32_t dx, std::int32_t dy, GpuSlot& src, std::int32_t sx, std::int32_t sy,
                         std::int64_t w, std::int64_t h, std::uint32_t mode, std::uint32_t alpha) {
        // clip to the source, then to the destination
        if (sx < 0) { dx -= sx; w += sx; sx = 0; }
        if (sy < 0) { dy -= sy; h += sy; sy = 0; }
        w = std::min<std::int64_t>(w, std::int64_t{src.info.width} - sx);
        h = std::min<std::int64_t>(h, std::int64_t{src.info.height} - sy);
        if (dx < 0) { sx -= dx; w += dx; dx = 0; }
        if (dy < 0) { sy -= dy; h += dy; dy = 0; }
        w = std::min<std::int64_t>(w, std::int64_t{dst.info.width} - dx);
        h = std::min<std::int64_t>(h, std::int64_t{dst.info.height} - dy);
        if (w <= 0 || h <= 0) return {};

        BlitParams p{};
        p.sizes[0] = dst.info.width;
        p.sizes[1] = dst.info.height;
        p.sizes[2] = src.info.width;
        p.sizes[3] = src.info.height;
        p.origin[0] = dx;
        p.origin[1] = dy;
        p.origin[2] = sx;
        p.origin[3] = sy;
        p.job[0] = static_cast<std::uint32_t>(w);
        p.job[1] = static_cast<std::uint32_t>(h);
        p.job[2] = mode | static_cast<std::uint32_t>(src.info.format) << 8 | static_cast<std::uint32_t>(dst.info.format) << 10 |
                   std::min<std::uint32_t>(alpha, 0x100) << 16;

        if (src.buffer != dst.buffer) return dispatch_blit(src.buffer, dst.buffer, p);

        // A kernel cannot read and write one buffer: go through a copy of the source slot.
        auto tmp = make_buffer(src.info.width * src.info.height);
        if (!tmp) return std::unexpected(tmp.error());
        auto c = ensure_cmd();
        if (!c) return std::unexpected(c.error());
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(*c);
        SDL_GPUBufferLocation from{src.buffer, 0}, to{*tmp, 0};
        SDL_CopyGPUBufferToBuffer(copy, &from, &to, src.info.width * src.info.height * 4, false);
        SDL_EndGPUCopyPass(copy);
        auto r = dispatch_blit(*tmp, dst.buffer, p);
        SDL_ReleaseGPUBuffer(device, *tmp);
        return r;
    }

    Result<std::vector<std::uint32_t>> download(GpuSlot& s) {
        const Uint32 bytes = s.info.width * s.info.height * 4;
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        ti.size = bytes;
        SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(device, &ti);
        if (!down) return std::unexpected(sdl_error("download buffer"));
        auto c = ensure_cmd();
        if (!c) {
            SDL_ReleaseGPUTransferBuffer(device, down);
            return std::unexpected(c.error());
        }
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(*c);
        SDL_GPUBufferRegion from{s.buffer, 0, bytes};
        SDL_GPUTransferBufferLocation to{down, 0};
        SDL_DownloadFromGPUBuffer(copy, &from, &to);
        SDL_EndGPUCopyPass(copy);
        if (auto f = flush(true); !f) {
            SDL_ReleaseGPUTransferBuffer(device, down);
            return std::unexpected(f.error());
        }
        const auto* data = static_cast<const std::uint32_t*>(SDL_MapGPUTransferBuffer(device, down, false));
        std::vector<std::uint32_t> out(data, data + bytes / 4);
        SDL_UnmapGPUTransferBuffer(device, down);
        SDL_ReleaseGPUTransferBuffer(device, down);
        return out;
    }
};

Display::~Display() = default;

Result<std::unique_ptr<Display>> Display::create(Renderer& renderer, std::uint32_t width, std::uint32_t height) {
    std::unique_ptr<Display> d(new Display);
    d->impl_ = std::make_unique<Impl>();
    Impl& m = *d->impl_;
    m.renderer = &renderer;
    m.device = renderer.native_device();

    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(m.device);
    auto make_pipeline = [&](const char* name, Uint32 ro_buf, Uint32 rw_buf, Uint32 rw_tex,
                             SDL_GPUComputePipeline*& out) -> Result<void> {
        auto blob = renderer.shaders().find(name, supported);
        if (!blob) return fail(Errc::Unsupported, std::string("no ") + name + " shader for the " + renderer.driver_name() + " backend");
        const std::string entry(blob->entry_point);
        SDL_GPUComputePipelineCreateInfo info{};
        info.code = blob->data.data();
        info.code_size = blob->data.size();
        info.entrypoint = entry.c_str();
        info.format = static_cast<SDL_GPUShaderFormat>(blob->format);
        info.num_readonly_storage_buffers = ro_buf;
        info.num_readwrite_storage_buffers = rw_buf;
        info.num_readwrite_storage_textures = rw_tex;
        info.num_uniform_buffers = 1;
        info.threadcount_x = 8;
        info.threadcount_y = 8;
        info.threadcount_z = 1;
        out = SDL_CreateGPUComputePipeline(m.device, &info);
        if (!out) return std::unexpected(sdl_error(name));
        return {};
    };
    if (auto r = make_pipeline("blit_rect", 1, 1, 0, m.blit_pipeline); !r) return std::unexpected(r.error());
    if (auto r = make_pipeline("slot_present", 1, 0, 1, m.present_pipeline); !r) return std::unexpected(r.error());
    if (auto r = make_pipeline("crossfade", 2, 1, 0, m.crossfade_pipeline); !r) return std::unexpected(r.error());

    if (auto r = make_pipeline("screen_filter", 1, 1, 0, m.filter_pipeline); !r) return std::unexpected(r.error());

    auto dummy = m.make_buffer(1);
    if (!dummy) return std::unexpected(dummy.error());
    m.dummy = *dummy;

    m.screen.info = {width, height, SlotFormat::Bgrx};
    auto sb = m.make_buffer(width * height);
    if (!sb) return std::unexpected(sb.error());
    m.screen.buffer = *sb;
    auto tex = renderer.create_target_texture(width, height);
    if (!tex) return std::unexpected(tex.error());
    m.screen_tex = std::move(*tex);
    return d;
}

Result<void> Display::resize_screen(std::uint32_t width, std::uint32_t height) {
    Impl& m = *impl_;
    if (!width || !height || std::uint64_t{width} * height > (1u << 26)) return fail(Errc::Range, "bad screen size");
    if (width == m.screen.info.width && height == m.screen.info.height) return {};
    if (auto r = m.flush(true); !r) return r;
    auto buffer = m.make_buffer(width * height);
    if (!buffer) return std::unexpected(buffer.error());
    auto tex = m.renderer->create_target_texture(width, height);
    if (!tex) { SDL_ReleaseGPUBuffer(m.device, *buffer); return std::unexpected(tex.error()); }
    if (auto r = m.renderer->resize_canvas(width, height); !r) {
        SDL_ReleaseGPUBuffer(m.device, *buffer);
        return r;
    }
    SDL_ReleaseGPUBuffer(m.device, m.screen.buffer);
    m.screen.buffer = *buffer;
    m.screen.info = {width, height, SlotFormat::Bgrx};
    m.screen_tex = std::move(*tex);
    return m.fill(m.screen, 0);
}

const SlotInfo* Display::slot(std::uint32_t id) const {
    auto it = impl_->slots.find(id);
    return it == impl_->slots.end() ? nullptr : &it->second.info;
}

std::uint64_t Display::slot_generation(std::uint32_t id) const {
    const auto it = impl_->slots.find(id);
    return it == impl_->slots.end() ? 0 : it->second.generation;
}

Result<void> Display::create_slot(std::uint32_t id, std::uint32_t w, std::uint32_t h, SlotFormat format) {
    Impl& m = *impl_;
    if (std::getenv("OBGI_SLOTS")) std::fprintf(stderr, "slot %u create %ux%u fmt %u\n", id, w, h, static_cast<unsigned>(format));
    if (w == 0 || h == 0 || std::uint64_t{w} * h > (1u << 26)) return fail(Errc::Range, "bad image slot size");
    auto buf = m.make_buffer(w * h);
    if (!buf) return std::unexpected(buf.error());
    if (auto it = m.slots.find(id); it != m.slots.end()) SDL_ReleaseGPUBuffer(m.device, it->second.buffer);
    m.stale(id);
    GpuSlot& s = m.slots[id];
    s.info = {w, h, format};
    s.buffer = *buf;
    s.generation = ++m.next_generation;
    return m.fill(s, 0);
}

Result<void> Display::free_slot(std::uint32_t id) {
    Impl& m = *impl_;
    auto it = m.slots.find(id);
    if (it == m.slots.end()) return fail(Errc::Range, "no such image slot");
    SDL_ReleaseGPUBuffer(m.device, it->second.buffer);
    m.slots.erase(it);
    m.stale(id);
    return {};
}

Result<void> Display::fill_slot(std::uint32_t id, std::uint32_t packed) {
    auto it = impl_->slots.find(id);
    if (it == impl_->slots.end()) return fail(Errc::Range, "no such image slot");
    impl_->stale(id);
    return impl_->fill(it->second, packed);
}

Result<void> Display::upload_slot(std::uint32_t id, std::uint32_t w, std::uint32_t h, SlotFormat format,
                                  std::span<const std::uint32_t> packed) {
    if (packed.size() < std::size_t{w} * h) return fail(Errc::Range, "slot pixel data too small");
    // Animated CPU-rendered effects upload their full-size result every frame.
    // Recreating the GPU buffer also cleared it before the upload, doubling work
    // and forcing needless driver allocations on every transition step.
    auto it = impl_->slots.find(id);
    if (it != impl_->slots.end() && it->second.info.width == w && it->second.info.height == h && it->second.info.format == format) {
        impl_->stale(id);
        return impl_->upload(it->second.buffer, packed.first(std::size_t{w} * h));
    }
    if (auto r = create_slot(id, w, h, format); !r) return r;
    return impl_->upload(impl_->slots[id].buffer, packed.first(std::size_t{w} * h));
}

Result<void> Display::blit(std::uint32_t dst, std::int32_t dx, std::int32_t dy, std::uint32_t src, std::int32_t sx,
                           std::int32_t sy, std::uint32_t w, std::uint32_t h, std::uint32_t mode, std::uint32_t alpha) {
    Impl& m = *impl_;
    auto d = m.slots.find(dst);
    auto s = m.slots.find(src);
    if (d == m.slots.end() || s == m.slots.end()) return fail(Errc::Range, "no such image slot");
    if (alpha > 0x100) return fail(Errc::Range, "alpha out of range");
    if (!blend_mode_supported(mode)) return fail(Errc::Unsupported, "blend mode " + std::to_string(mode) + " is not implemented");
    m.stale(dst);
    return m.do_blit(d->second, dx, dy, s->second, sx, sy, w, h, mode, alpha);
}

Result<std::vector<std::uint32_t>> Display::read_slot(std::uint32_t id) {
    auto it = impl_->slots.find(id);
    if (it == impl_->slots.end()) return fail(Errc::Range, "no such image slot");
    return impl_->download(it->second);
}

Sprite* Display::find_sprite(std::uint32_t id) { return scene_.find_sprite(id); }
void Display::set_visible(std::uint32_t id, bool value, int depth) { scene_.set_visible(id, value, depth); }
void Display::set_level(std::uint32_t id, std::uint32_t value, int depth) { scene_.set_level(id, value, depth); }
void Display::set_progress(std::uint32_t id, std::uint32_t value, int depth) { scene_.set_progress(id, value, depth); }
void Display::set_fade(std::uint32_t id, std::uint32_t value, int depth) { scene_.set_fade(id, value, depth); }
void Display::set_opacity(std::uint32_t id, std::uint32_t value, int depth) { scene_.set_opacity(id, value, depth); }
void Display::resolve_masters() { scene_.resolve_masters(); }

// Renders the current background type (bg_fx) on the CPU into the temporary slot 0x7F400000 that the lower background sprite shows
// (gfx/background/*.c: each type's render slot). A picture that is gone or does not fit paints black, as in the reference.
Result<void> Display::render_background_fx() {
    Impl& m = *impl_;
    constexpr std::uint32_t kFxSlot = 0x7F400000u;
    Sprite* up = find_sprite(kBackgroundUpper);
    Sprite* lo = find_sprite(kBackgroundLower);
    if (!bg_fx.type || !up || !lo) return {};
    const std::uint32_t w = m.screen.info.width, h = m.screen.info.height;
    const std::int32_t level = static_cast<std::int32_t>(up->alpha);
    const std::int32_t px = up->script_x(), py = up->script_y();
    std::vector<std::int64_t> key = {bg_fx.type, std::int64_t(bg_fx.revision), level, px, py, up->fade, up->opacity, w, h,
                                     std::int64_t(vecmap_epoch), bg_fx.amount, bg_fx.rings, bg_fx.ripple_no, bg_fx.selector};
    const auto version_of = [&](std::int32_t id) {
        const auto v = m.versions.find(static_cast<std::uint32_t>(id));
        key.push_back(v == m.versions.end() ? 0 : static_cast<std::int64_t>(v->second));
    };
    for (std::int32_t id : bg_fx.bmp) version_of(id);
    version_of(bg_fx.second);
    for (std::int32_t id : bg_fx.frames) version_of(id);
    lo->slot = kFxSlot, lo->x = lo->y = 0, lo->priority = 0, lo->blend = 1, lo->alpha = 0, lo->background = true, lo->flip_position = false;
    lo->through_gray = false, lo->wipe_gray = 0xFFFFFFFFu, lo->progress = 0x100;
    lo->visible = bg_visible && bg_mode != 0;
    up->slot = 0xFFFFFFFFu, up->background = true, up->visible = false; // only drives the level and the position
    if (m.slots.count(kFxSlot) && m.bg_key == key) return {};

    // a picture as pixels, if it is there and is in a format the kernels know; any size unless `screen_sized`
    auto image = [&](std::int32_t id, bool screen_sized) -> const bgfx::Image* {
        if (id < 0 || id >= 0x4000) return nullptr;
        const auto it = m.slots.find(static_cast<std::uint32_t>(id));
        if (it == m.slots.end()) return nullptr;
        if (it->second.info.format != SlotFormat::Bgra && it->second.info.format != SlotFormat::Bgrx) return nullptr;
        if (screen_sized && (it->second.info.width != w || it->second.info.height != h)) return nullptr;
        auto cached = m.bg_images.find(static_cast<std::uint32_t>(id));
        if (cached == m.bg_images.end()) {
            auto px_data = m.download(it->second);
            if (!px_data) return nullptr;
            cached = m.bg_images.emplace(static_cast<std::uint32_t>(id), bgfx::Image{it->second.info.width, it->second.info.height, std::move(*px_data)}).first;
        }
        return &cached->second;
    };
    bgfx::Image out{w, h, std::vector<std::uint32_t>(std::size_t{w} * h, 0xFF000000u)}; // black unless a type draws something
    const auto colour_second = [&] { return bg_fx.second == 0x7000 || bg_fx.second == 0x7001; };
    switch (bg_fx.type) {
    case 2: {
        const bgfx::Image* first = image(bg_fx.bmp[0], true);
        const bgfx::Image* second = colour_second() ? nullptr : image(bg_fx.second, true);
        if (!first || (!colour_second() && !second)) break;
        // effect 1 (alpha blend): the effective level combines the fade and the opacity (dispobj.c: DispObj_EffectiveLevel)
        const std::int32_t eff = 0x100 - static_cast<std::int32_t>(((0x100u - std::min(up->fade, 0x100u)) * (0x100u - std::min<std::uint32_t>(std::max(level, 0), 0x100u)) * up->opacity) >> 16);
        out = colour_second() ? bgfx::toward_colour(*first, bg_fx.second == 0x7001, eff) : bgfx::crossfade(*first, *second, eff);
        break;
    }
    case 3: {
        std::array<const bgfx::Image*, 4> q{};
        for (int i = 0; i < 4; ++i) q[i] = image(bg_fx.bmp[i], true);
        if (!q[0] || !q[1] || !q[2] || !q[3]) break;
        // setPos moves the origin; a position out of range is ignored (tiled.c: Bg3_SetOrigin)
        if (px >= 0 && py >= 0 && px <= static_cast<std::int32_t>(w) && py <= static_cast<std::int32_t>(h)) bg_fx.origin_x = px, bg_fx.origin_y = py;
        out = bgfx::tiled(q, bg_fx.origin_x, bg_fx.origin_y);
        break;
    }
    case 5: {
        if (level < 0 || static_cast<std::size_t>(level) >= bg_fx.frames.size()) break;
        if (const bgfx::Image* f = image(bg_fx.frames[static_cast<std::size_t>(level)], true)) out = *f;
        break;
    }
    case 6: { // displace.c: Bg6_Render, the picture through one vector map or the mix of two by the effective level
        const bgfx::Image* src = image(bg_fx.bmp[0], true);
        const auto v1 = vecmaps.find(static_cast<std::uint32_t>(bg_fx.bmp[1]));
        if (!src || v1 == vecmaps.end()) break;
        const vecmap::Map* v2 = nullptr;
        if (bg_fx.bmp[2] != -1) {
            const auto it = vecmaps.find(static_cast<std::uint32_t>(bg_fx.bmp[2]));
            if (it == vecmaps.end()) break;
            v2 = &it->second;
        }
        const std::int32_t eff = 0x100 - static_cast<std::int32_t>(((0x100u - std::min(up->fade, 0x100u)) * (0x100u - std::min<std::uint32_t>(std::max(level, 0), 0x100u)) * up->opacity) >> 16);
        bgfx::Image shifted;
        if (vecmap::displace(shifted, *src, v1->second, v2, eff, bg_fx.amount) == 0) out = std::move(shifted);
        break;
    }
    case 7: {
        if (const bgfx::Image* src = image(bg_fx.bmp[0], true)) out = bgfx::box_blur(*src, bg_fx.edge, level);
        break;
    }
    case 8: { // ripple.c: Bg8_Render, the picture through the vector + distance map and the ring table filled for the level (the amplitude)
        const bgfx::Image* src = image(bg_fx.bmp[0], true);
        const auto vd = vecmaps.find(static_cast<std::uint32_t>(bg_fx.bmp[1]));
        if (!src || vd == vecmaps.end()) break;
        std::vector<std::uint32_t> table;
        if (vecmap::ripple_fill(ripples, table, bg_fx.ripple_no, bg_fx.selector, level, bg_fx.rings) != 0) break;
        bgfx::Image rippled;
        if (vecmap::ripple_blit(rippled, *src, vd->second, table) == 0) out = std::move(rippled);
        break;
    }
    case 9: { // stretch.c: Bg9_Render, in the original's order of doubles
        const bgfx::Image* src = image(bg_fx.bmp[0], false);
        if (!src) break;
        const double lv = static_cast<double>(static_cast<std::uint32_t>(level));
        const double wp = static_cast<double>(static_cast<std::uint32_t>(bg_fx.view_w)) + (static_cast<double>(bg_fx.target_w - bg_fx.view_w) * lv) * 0.00390625;
        const double hp = static_cast<double>(static_cast<std::uint32_t>(bg_fx.view_h)) + (static_cast<double>(bg_fx.target_h - bg_fx.view_h) * lv) * 0.00390625;
        if (wp <= 0 || hp <= 0) break;
        const auto trunc = [](double v) { return static_cast<std::int32_t>(v); };
        const std::int32_t a = trunc(static_cast<double>(h * 65540u) / hp), b = trunc(static_cast<double>(w * 65540u) / wp);
        const std::int32_t c = trunc(hp * 65536.0), d = trunc(wp * 65536.0);
        const std::int32_t y = trunc((static_cast<double>(static_cast<std::int32_t>((static_cast<std::uint32_t>(bg_fx.target_y) << 16) - static_cast<std::uint32_t>(py))) * lv) * 0.00390625 + py);
        const std::int32_t x = trunc((static_cast<double>(static_cast<std::int32_t>((static_cast<std::uint32_t>(bg_fx.target_x) << 16) - static_cast<std::uint32_t>(px))) * lv) * 0.00390625 + px);
        out = bgfx::scale2(*src, w, h, x, y, d, c, b, a);
        break;
    }
    case 10: { // zoom.c: Bg10_Render
        const bgfx::Image* src = image(bg_fx.bmp[0], false);
        if (!src) break;
        std::int32_t zoom = bg_fx.scale;
        if (bg_fx.scale_delta != 0 && level != 0) {
            const std::int32_t delta = bg_fx.scale_delta, ad = delta < 0 ? static_cast<std::int32_t>(0u - static_cast<std::uint32_t>(delta)) : delta;
            const double s256 = std::sin(static_cast<double>(static_cast<std::uint32_t>(level)) * 0.006135923151542565) * 256.0;
            const double q = static_cast<double>(delta) / (static_cast<double>(ad) - ((static_cast<double>(static_cast<std::int32_t>(static_cast<std::uint32_t>(ad) - 65536u)) * s256) * 0.00390625));
            const double sign = delta < 0 ? 1.0 : -1.0;
            zoom = static_cast<std::int32_t>(static_cast<double>(static_cast<std::uint32_t>(bg_fx.scale)) + ((((256.0 - s256) * sign) * 0.00390625) + q) * 65536.0);
        }
        const std::int32_t angle = bg_fx.angle + static_cast<std::int32_t>((std::int64_t{bg_fx.angle_delta} * static_cast<std::uint32_t>(level)) >> 8);
        out = bgfx::rotate_centred(*src, w, h, zoom, angle);
        break;
    }
    case 11: { // mosaic.c: Bg11_Render
        const bgfx::Image* front = image(bg_fx.bmp[0], true);
        if (!front) break;
        bgfx::Image dst = out;
        if (colour_second()) {
            if (bg_fx.link == 1) std::fill(dst.px.begin(), dst.px.end(), bg_fx.second == 0x7000 ? 0u : 0xFFFFFFu);
        } else if (bg_fx.link == 1) {
            if (const bgfx::Image* back = image(bg_fx.second, true)) bgfx::mosaic(dst, *back, 0x100 - level, 0);
        }
        bgfx::mosaic(dst, *front, level, bg_fx.link == 1 ? level : 0);
        out = std::move(dst);
        break;
    }
    default: break;
    }
    if (auto r = upload_slot(kFxSlot, w, h, SlotFormat::Bgra, out.px); !r) return r;
    m.bg_key = key;
    return {};
}

// Renders every tile map's window on the CPU (map.c: Map_Refresh + Map_Draw): the cell under each pixel shows its tile of the chipset,
// an empty or out-of-range tile clears the cell; with `wrap` the terrain repeats past its edges, otherwise the cells beyond it are empty.
Result<void> Display::render_maps() {
    Impl& m = *impl_;
    for (auto& [id, md] : maps) {
        Sprite* sp = find_sprite(id);
        if (!sp) continue;
        const auto chip_it = md.chipset >= 0 ? m.slots.find(static_cast<std::uint32_t>(md.chipset)) : m.slots.end();
        if (!md.sized || !md.view_valid || chip_it == m.slots.end() ||
            (chip_it->second.info.format != SlotFormat::Bgra && chip_it->second.info.format != SlotFormat::Bgrx)) {
            sp->slot = 0xFFFFFFFFu; // nothing to draw from yet (map.c: Map_Draw returns early)
            continue;
        }
        const std::uint32_t tmp_id = 0x7F700000u | (id & 0xFFu);
        const auto version = m.versions.find(static_cast<std::uint32_t>(md.chipset));
        const std::vector<std::int64_t> key = {static_cast<std::int64_t>(md.revision), version == m.versions.end() ? 0 : static_cast<std::int64_t>(version->second)};
        if (!m.slots.count(tmp_id) || md.key != key) {
            auto sheet = m.download(chip_it->second);
            if (!sheet) return std::unexpected(sheet.error());
            const std::uint32_t sw = chip_it->second.info.width, per_row = sw / static_cast<std::uint32_t>(md.chip_w);
            const std::uint32_t chips = per_row * (chip_it->second.info.height / static_cast<std::uint32_t>(md.chip_h));
            const std::uint32_t vw = static_cast<std::uint32_t>(md.cols * md.chip_w), vh = static_cast<std::uint32_t>(md.rows * md.chip_h);
            std::vector<std::uint32_t> out(std::size_t{vw} * vh, 0);
            for (std::uint32_t y = 0; y < vh; ++y)
                for (std::uint32_t x = 0; x < vw; ++x) {
                    const std::int32_t px = static_cast<std::int32_t>(x) + md.off_x, py = static_cast<std::int32_t>(y) + md.off_y;
                    std::int64_t tx = std::int64_t{md.col} + px / md.chip_w, ty = std::int64_t{md.row} + py / md.chip_h;
                    if (md.wrap) tx %= md.terr_w, ty %= md.terr_h;
                    else if (tx >= md.terr_w || ty >= md.terr_h) continue;
                    const std::uint32_t tile = md.terrain[static_cast<std::size_t>(ty * md.terr_w + tx)];
                    if (tile == 0xFFFFu || tile >= chips) continue;
                    const std::uint32_t sx = (tile % per_row) * static_cast<std::uint32_t>(md.chip_w) + static_cast<std::uint32_t>(px % md.chip_w);
                    const std::uint32_t sy = (tile / per_row) * static_cast<std::uint32_t>(md.chip_h) + static_cast<std::uint32_t>(py % md.chip_h);
                    out[std::size_t{y} * vw + x] = (*sheet)[std::size_t{sy} * sw + sx];
                }
            if (auto r = upload_slot(tmp_id, vw, vh, chip_it->second.info.format, out); !r) return r;
            md.key = key;
        }
        sp->slot = tmp_id;
    }
    return {};
}

bool Display::screen_rect(std::uint32_t id, std::int32_t& left, std::int32_t& top, std::int32_t& right, std::int32_t& bottom) {
    Sprite* sp = find_sprite(id);
    if (!sp) return false;
    const auto it = impl_->slots.find(sp->slot);
    if (it == impl_->slots.end()) return false;
    std::int64_t w = it->second.info.width, h = it->second.info.height, x = sp->x + sp->off_x + sp->off2_x, y = sp->y + sp->off_y + sp->off2_y;
    if (sp->projected) {
        const std::int64_t k = project_scale(sp->depth, sp->no_perspective ? 0 : sp->dist);
        const auto scaled = [k](std::int64_t v) { return v * k >> 16; };
        x = std::int64_t{impl_->screen.info.width / 2} + (sp->scale_position ? scaled(sp->x) : sp->x) - scaled(sp->origin_x) + sp->off_x + sp->off2_x;
        y = std::int64_t{impl_->screen.info.height / 2} + (sp->scale_position ? scaled(sp->y) : sp->y) - scaled(sp->origin_y) + sp->off_y + sp->off2_y;
        w = std::max<std::int64_t>(1, scaled(w)), h = std::max<std::int64_t>(1, scaled(h));
    } else if (sp->xform) {
        x -= std::int64_t{sp->origin_x} * sp->sx >> 16, y -= std::int64_t{sp->origin_y} * sp->sy >> 16;
        w = std::max<std::int64_t>(1, w * sp->sx >> 16), h = std::max<std::int64_t>(1, h * sp->sy >> 16);
    }
    left = static_cast<std::int32_t>(x), top = static_cast<std::int32_t>(y), right = static_cast<std::int32_t>(x + w - 1), bottom = static_cast<std::int32_t>(y + h - 1);
    return true;
}

Result<void> Display::compose(std::optional<std::uint32_t> max_priority) {
    Impl& m = *impl_;
    if (auto r = m.fill(m.screen, 0); !r) return r;
    resolve_masters();
    if (auto r = render_background_fx(); !r) return r;
    if (auto r = render_maps(); !r) return r;
    std::vector<const std::pair<const std::uint32_t, Sprite>*> order;
    // 90:0C: a switch and a transparency for every window, and for what is attached to one
    auto in_window = [this](std::uint32_t id) {
        for (int depth = 0; depth < 16; ++depth) {
            if ((id >> 24) == 0xB0) return true;
            const Sprite* sp = const_cast<Display*>(this)->find_sprite(id);
            if (!sp || !sp->has_master) return false;
            id = sp->master;
        }
        return false;
    };
    std::set<std::uint32_t> window_parts;
    if (!windows_shown || windows_level)
        for (const auto& kv : scene_.objects())
            if (in_window(kv.first)) window_parts.insert(kv.first);
    for (const auto& kv : scene_.objects())
        if (kv.second.is_shown() && (max_priority || kv.second.priority >= draw_limit || kv.second.background) &&
            !(!windows_shown && window_parts.count(kv.first)))
            order.push_back(&kv);
    // the original's sort key: (priority * 16 + class order) << 16, plus the bias, plus the creation counter
    auto class_order = [](std::uint32_t id, const Sprite& s) -> std::uint32_t {
        if (s.background) return 0;
        switch (id >> 24) {
        case 0xA0: return 1;
        case 0xB0: return 3;
        case 0xC0: return 4;
        case 0xC1: return 5;
        case 0x91: return 6;
        case 0x90: return 7;
        case 0xF1: return 9;
        case 0xF0: return 10;
        default: return 2; // sprites
        }
    };
    auto sort_key = [&](const std::pair<const std::uint32_t, Sprite>* kv) -> std::int64_t {
        const Sprite& sp = kv->second;
        // sort mode 1 (dispobj.c: DispObj_SortKey): the priority, then the depth over its whole range, no class order
        if (sp.sort_mode == 1) return (std::int64_t{sp.priority} << 16) + static_cast<std::uint32_t>(sp.sort_bias - (sp.depth >> 16) + 0x7fff);
        const std::int64_t hi = (std::int64_t{sp.priority} * 16 + class_order(kv->first, sp)) * 65536 + sp.sort_bias;
        // These are two layers of one logical background, independent of allocation order.
        if (kv->first == kBackgroundLower || kv->first == kBackgroundUpper) return hi + (kv->first == kBackgroundUpper ? 1 : 0);
        // a projected plane is ordered by depth, a larger z (farther away) sorting lower; the others by creation order
        if (sp.projected) return hi + ((0xffff8000u - static_cast<std::uint32_t>(static_cast<std::uint16_t>(sp.proj_depth() >> 16))) & 0xffffu);
        return hi + (scene_.creation_order(kv->first) & 0x7FFF);
    };
    std::stable_sort(order.begin(), order.end(), [&](auto* a, auto* b) { return sort_key(a) < sort_key(b); });
    for (const auto* kv : order) {
        if (max_priority && sort_key(kv) > (std::int64_t{*max_priority} * 0x100000 + 0xFFFFF)) continue;
        const Sprite& sp = kv->second;
        if (std::getenv("OBGI_SPRITES")) std::fprintf(stderr, "sprite %X slot %u at %d,%d blend %u alpha %u prio %u%s""\n", kv->first, sp.slot, sp.x, sp.y, sp.blend, sp.alpha, sp.priority, m.slots.count(sp.slot) ? "" : " (no slot)");
        if (sp.filter_kind >= 0) {
            // filter.c: a colour operation on everything drawn so far. The filter's effect is 0xC0, the strength family, so the level
            // is ((0x100 - fade) * level * opacity) >> 16 (dispobj.c: DispObj_EffectiveLevel); with a gray bitmap (90:66) every kind is
            // the per-pixel tint of Blit_TintByGray. Each pixel stays on the GPU.
            const std::int32_t level = static_cast<std::int32_t>(((0x100u - std::min(sp.fade, 0x100u)) * std::min(sp.alpha, 0x100u) * sp.opacity) >> 16);
            const auto gi = sp.filter_gray != 0xFFFFFFFFu ? m.slots.find(sp.filter_gray) : m.slots.end();
            const bool by_gray = gi != m.slots.end() && gi->second.info.format == SlotFormat::Gray8 && gi->second.info.width == m.screen.info.width &&
                                 gi->second.info.height == m.screen.info.height;
            if (sp.filter_gray != 0xFFFFFFFFu && !by_gray) continue; // the bitmap was freed or replaced: nothing is drawn
            if (!by_gray && (sp.filter_kind > 3 || level <= 0)) continue;
            if (auto r = m.filter(sp, level, by_gray ? gi->second.buffer : nullptr); !r) return r;
            continue;
        }
        if (sp.effector) {
            // effector.c: Effector_Draw, a snapshot of everything drawn so far written back distorted; the strength is the effective level
            // (the ripple amplitude and the zoom blend use the raw level)
            const Sprite::EffectorState& e = sp.fx;
            const std::int32_t eff = 0x100 - static_cast<std::int32_t>(((0x100u - std::min(sp.fade, 0x100u)) * (0x100u - std::min(sp.alpha, 0x100u)) * sp.opacity) >> 16);
            auto px_data = m.download(m.screen);
            if (!px_data) return std::unexpected(px_data.error());
            const std::uint32_t sw = m.screen.info.width, sh = m.screen.info.height;
            const bgfx::Image snap{sw, sh, std::move(*px_data)};
            bgfx::Image out;
            bool drawn = false;
            switch (e.mode) {
            case 0: {
                const auto v1 = vecmaps.find(static_cast<std::uint32_t>(e.vec1));
                const auto v2 = e.vec2 != -1 ? vecmaps.find(static_cast<std::uint32_t>(e.vec2)) : vecmaps.end();
                if (v1 == vecmaps.end() || (e.vec2 != -1 && v2 == vecmaps.end())) break; // a freed map leaves the screen as it is
                drawn = vecmap::displace(out, snap, v1->second, e.vec2 != -1 ? &v2->second : nullptr, eff, e.amount) == 0;
                break;
            }
            case 1:
                if (e.grad_type > 1 || eff > 0x100) break;
                out = bgfx::box_blur(snap, e.grad_type, eff);
                drawn = true;
                break;
            case 2: {
                const auto vd = vecmaps.find(static_cast<std::uint32_t>(e.map));
                std::vector<std::uint32_t> table;
                if (vd == vecmaps.end() || vecmap::ripple_fill(ripples, table, e.ripple_no, e.ripple_sel, static_cast<std::int32_t>(sp.alpha), e.rings) != 0) break;
                drawn = vecmap::ripple_blit(out, snap, vd->second, table) == 0;
                break;
            }
            case 3: { // Effector_RecalcZoom: the deltas scaled by the progress, the level blending toward that transform from the identity
                const auto mul_shr = [](std::int64_t a, std::uint32_t b, int s) { return static_cast<std::int64_t>(static_cast<std::uint64_t>(a) * b) >> s; };
                const std::uint32_t t = std::min(sp.progress, 0x100u) << 16, level = std::min(sp.alpha, 0x100u);
                const std::int32_t cur_cx = e.cx + static_cast<std::int32_t>(mul_shr(e.d_cx, t, 24)), cur_cy = e.cy + static_cast<std::int32_t>(mul_shr(e.d_cy, t, 24));
                const std::int64_t a = ((std::int64_t{bgfx::ease(static_cast<std::int32_t>(t), e.curve)} * e.d_angle) >> 16) + e.angle;
                const std::int32_t cur_angle = static_cast<std::int32_t>(mul_shr(a, level, 8));
                std::int64_t s = mul_shr(e.d_sx, t, 24) + std::int64_t{static_cast<std::uint32_t>(e.sx)} - 0x10000;
                const std::int32_t cur_sx = 0x10000 + static_cast<std::int32_t>(mul_shr(s, level, 8));
                s = mul_shr(e.d_sy, t, 24) + std::int64_t{static_cast<std::uint32_t>(e.sy)} - 0x10000;
                const std::int32_t cur_sy = 0x10000 + static_cast<std::int32_t>(mul_shr(s, level, 8));
                // the snapshot point (cur_cx, cur_cy) lands on the screen centre; every pixel is replaced
                const bgfx::Transformed tr = bgfx::transform(snap, cur_cx, cur_cy, cur_angle, cur_sx, cur_sy, e.smooth != 0);
                out = bgfx::Image{sw, sh, std::vector<std::uint32_t>(std::size_t{sw} * sh, 0)};
                const std::int32_t ax = static_cast<std::int32_t>(sw & ~1u) / 2 - tr.anchor_x, ay = static_cast<std::int32_t>(sh & ~1u) / 2 - tr.anchor_y;
                for (std::int32_t y = 0; y < static_cast<std::int32_t>(tr.image.h); ++y)
                    for (std::int32_t x = 0; x < static_cast<std::int32_t>(tr.image.w); ++x) {
                        const std::int32_t dx = ax + x, dy = ay + y;
                        if (dx >= 0 && dy >= 0 && dx < static_cast<std::int32_t>(sw) && dy < static_cast<std::int32_t>(sh))
                            out.px[std::size_t(dy) * sw + std::size_t(dx)] = tr.image.px[std::size_t(y) * tr.image.w + std::size_t(x)];
                    }
                drawn = true;
                break;
            }
            default: break;
            }
            if (drawn)
                if (auto up_r = m.upload(m.screen.buffer, out.px); !up_r) return up_r;
            continue;
        }
        std::uint32_t draw_id = sp.slot;
        const std::int32_t draw_x = sp.x + sp.off_x + sp.off2_x, draw_y = sp.y + sp.off_y + sp.off2_y;
        if (sp.wipe_gray == 0xFFFFFFFFu || (sp.through_gray ? sp.progress == 0 : sp.progress >= 0x100)) {
            m.wipes.erase(kv->first);
        } else { // a wipe sprite part-way: the picture's alpha is scaled by how far the gray pattern has been passed (blit_xform.c: GrayWipeTo)
            if (!sp.through_gray && sp.progress == 0) continue;
            auto s0 = m.slots.find(sp.slot);
            auto g0 = m.slots.find(sp.wipe_gray);
            if (s0 != m.slots.end() && g0 != m.slots.end() &&
                (sp.through_gray || (s0->second.info.width == g0->second.info.width && s0->second.info.height == g0->second.info.height))) {
                Impl::WipeCache& wc = m.wipes[kv->first];
                bool fresh = wc.src_px.empty() || wc.src != sp.slot || wc.gray != sp.wipe_gray;
                if (fresh) {
                    auto a = m.download(s0->second);
                    auto b = m.download(g0->second);
                    if (a && b) {
                        wc.src_px = std::move(*a), wc.gray_px = std::move(*b), wc.src = sp.slot, wc.gray = sp.wipe_gray;
                        wc.progress = 0xFFFFFFFFu;
                    }
                }
                const std::uint32_t tmp_id = 0x7F100000u | (kv->first & 0xFFFFFu);
                if (!wc.src_px.empty() && (wc.progress != sp.progress || wc.mode != sp.wipe_mode || wc.through_gray != sp.through_gray ||
                                           (sp.through_gray && (wc.x != draw_x || wc.y != draw_y)))) {
                    const bool has_alpha = s0->second.info.format == SlotFormat::Bgra;
                    const std::uint32_t sw = s0->second.info.width, gw = g0->second.info.width, gh = g0->second.info.height;
                    std::vector<std::uint32_t> out(wc.src_px.size());
                    if (!sp.through_gray) {
                        const int mode = static_cast<int>(sp.wipe_mode & 7), thr = ((1 << mode) + 1) * static_cast<int>(sp.progress);
                        for (std::size_t i = 0; i < out.size(); ++i) {
                            int v = thr - (static_cast<int>(wc.gray_px[i] & 0xFF) << mode);
                            v = std::clamp(v, 0, 0x100);
                            const std::uint32_t a = has_alpha ? wc.src_px[i] >> 24 : 0xFF;
                            out[i] = (wc.src_px[i] & 0xFFFFFFu) | (((a * static_cast<std::uint32_t>(v)) >> 8) << 24);
                        }
                    } else {
                        // blit_filter.c Blit_ThroughGray: the map is aligned with the screen; parameters 0..7 give a threshold of
                        // that width, 8..15 a triangle wave of 2p + 1 segments
                        const int param = static_cast<int>(sp.wipe_mode & 15), level = static_cast<int>(std::min(sp.progress, 0x100u));
                        int table[0x100] = {};
                        if (param >= 8) {
                            const int p = param & 7;
                            const double seg = 256.0 / (2 * p + 1), k = 1.0 / seg;
                            for (int i = 0; i < 0x100; ++i) {
                                const int n = static_cast<int>(i * k);
                                double f = i - n * seg;
                                if (n & 1) f = seg - f;
                                table[i] = static_cast<int>(k * f * 256.0);
                            }
                        }
                        const int base = 0x100 - ((1 << param) + 1) * level, bias = (0x80 - level) * 2;
                        for (std::size_t i = 0; i < out.size(); ++i) {
                            const std::int64_t gx = std::int64_t(draw_x) + std::int64_t(i % sw), gy = std::int64_t(draw_y) + std::int64_t(i / sw);
                            int v = 0; // outside the map nothing is revealed
                            if (gx >= 0 && gy >= 0 && gx < gw && gy < gh) {
                                const int g = static_cast<int>(wc.gray_px[std::size_t(gy) * gw + std::size_t(gx)] & 0xFF);
                                if (param < 8) {
                                    v = std::clamp((g << param) + base, 0, 0x100);
                                } else {
                                    const int idx = g + bias;
                                    v = idx <= 0 ? 0 : idx >= 0x100 ? 0x100 : table[idx];
                                }
                            }
                            const std::uint32_t a = has_alpha ? wc.src_px[i] >> 24 : 0xFF;
                            out[i] = (wc.src_px[i] & 0xFFFFFFu) | (((a * static_cast<std::uint32_t>(v)) >> 8) << 24);
                        }
                    }
                    if (upload_slot(tmp_id, s0->second.info.width, s0->second.info.height, SlotFormat::Bgra, out)) {
                        wc.progress = sp.progress, wc.mode = sp.wipe_mode, wc.through_gray = sp.through_gray, wc.x = draw_x, wc.y = draw_y;
                    }
                }
                if (m.slots.count(tmp_id) && wc.progress == sp.progress) draw_id = tmp_id;
            }
        }
        std::int32_t blit_x = draw_x, blit_y = draw_y;
        const auto version_of = [&m](std::uint32_t id) {
            const auto v = m.versions.find(id);
            return v == m.versions.end() ? std::int64_t{0} : static_cast<std::int64_t>(v->second);
        };
        // sprite.c mode 4: the picture read through the ripple's vector + distance map and ring table (Blit_RippleLevel) into a temporary slot
        if (sp.ripple_map != -1 && draw_id == sp.slot) {
            const auto s0 = m.slots.find(sp.slot);
            const auto vd = vecmaps.find(static_cast<std::uint32_t>(sp.ripple_map));
            if (s0 != m.slots.end() && vd != vecmaps.end() && (s0->second.info.format == SlotFormat::Bgra || s0->second.info.format == SlotFormat::Bgrx)) {
                Impl::TempCache& rc = m.mixes[kv->first | 0x80000000u];
                const std::uint32_t tmp_id = 0x7F800000u | (kv->first & 0xFFFFFu);
                const std::vector<std::int64_t> key = {sp.slot, sp.ripple_map, sp.rings, sp.ripple_no, sp.ripple_sel, sp.alpha, std::int64_t(vecmap_epoch), version_of(sp.slot)};
                if (!m.slots.count(tmp_id) || rc.key != key) {
                    std::vector<std::uint32_t> table;
                    auto px_data = m.download(s0->second);
                    bgfx::Image rippled;
                    if (px_data && vecmap::ripple_fill(ripples, table, sp.ripple_no, sp.ripple_sel, static_cast<std::int32_t>(sp.alpha), sp.rings) == 0 &&
                        vecmap::ripple_blit(rippled, bgfx::Image{s0->second.info.width, s0->second.info.height, std::move(*px_data)}, vd->second, table) == 0 &&
                        upload_slot(tmp_id, rippled.w, rippled.h, s0->second.info.format, rippled.px))
                        rc.key = key;
                }
                if (m.slots.count(tmp_id) && rc.key == key) draw_id = tmp_id;
            }
        }
        // sprite.c modes 1 and 5: a second picture is cross-faded in first (Blit_CrossfadeTo)
        if (sp.slot2 != 0xFFFFFFFFu && sp.mix_ratio > 0 && draw_id == sp.slot) {
            const auto a0 = m.slots.find(sp.slot), b0 = m.slots.find(sp.slot2);
            if (a0 != m.slots.end() && b0 != m.slots.end() && a0->second.info.width == b0->second.info.width && a0->second.info.height == b0->second.info.height &&
                a0->second.info.format == b0->second.info.format && (a0->second.info.format == SlotFormat::Bgra || a0->second.info.format == SlotFormat::Bgrx)) {
                Impl::TempCache& mc = m.mixes[kv->first];
                const std::uint32_t tmp_id = 0x7F500000u | (kv->first & 0xFFFFFu);
                const std::vector<std::int64_t> key = {sp.slot, sp.slot2, sp.mix_ratio, version_of(sp.slot), version_of(sp.slot2)};
                if (!m.slots.count(tmp_id) || mc.key != key) {
                    const auto tmp = m.slots.find(tmp_id);
                    if (tmp == m.slots.end() || tmp->second.info.width != a0->second.info.width ||
                        tmp->second.info.height != a0->second.info.height || tmp->second.info.format != a0->second.info.format) {
                        if (auto result = create_slot(tmp_id, a0->second.info.width, a0->second.info.height, a0->second.info.format); !result) return result;
                    }
                    if (auto result = m.crossfade(m.slots.at(tmp_id), a0->second, b0->second, std::min(sp.mix_ratio, 0x100u)); !result) return result;
                    mc.key = key;
                    m.scales.erase(kv->first);
                }
                if (m.slots.count(tmp_id) && mc.key == key) draw_id = tmp_id;
            }
        }
        bool xformed = false;
        // sprite.c modes 2 and 5 with a rotation or unequal scales: the picture is transformed on the CPU into a temporary slot
        if (sp.xform || (sp.projected && sp.angle != 0)) {
            const auto s0 = m.slots.find(draw_id);
            if (s0 != m.slots.end() && (s0->second.info.format == SlotFormat::Bgra || s0->second.info.format == SlotFormat::Bgrx)) {
                const std::int32_t k = sp.projected ? project_scale(sp.proj_depth(), sp.no_perspective ? 0 : sp.dist) : 0;
                const std::int32_t kx = sp.projected ? k : sp.sx, ky = sp.projected ? k : sp.sy;
                const auto scaled = [k](std::int64_t v) { return static_cast<std::int32_t>(v * k >> 16); };
                std::int32_t pos_x = sp.x, pos_y = sp.y;
                if (sp.projected) {
                    pos_x = static_cast<std::int32_t>(m.screen.info.width / 2) + (sp.scale_position ? scaled(sp.proj_x()) : sp.proj_x());
                    pos_y = static_cast<std::int32_t>(m.screen.info.height / 2) + (sp.scale_position ? scaled(sp.proj_y()) : sp.proj_y());
                }
                Impl::TempCache& xc = m.xforms[kv->first];
                const std::uint32_t tmp_id = 0x7F600000u | (kv->first & 0xFFFFFu);
                const std::vector<std::int64_t> key = {draw_id, sp.slot, sp.slot2, sp.mix_ratio, version_of(sp.slot), version_of(sp.slot2), kx, ky, sp.angle,
                                                       sp.origin_x, sp.origin_y, sp.smooth ? 1 : 0};
                if (!m.slots.count(tmp_id) || xc.key != key) {
                    if (auto px_data = m.download(s0->second)) {
                        const bgfx::Image src{s0->second.info.width, s0->second.info.height, std::move(*px_data)};
                        const bgfx::Transformed t = bgfx::transform(src, static_cast<std::int32_t>(static_cast<std::uint32_t>(sp.origin_x) << 16),
                                                                    static_cast<std::int32_t>(static_cast<std::uint32_t>(sp.origin_y) << 16), sp.angle, kx, ky, sp.smooth);
                        if (!t.image.px.empty() && upload_slot(tmp_id, t.image.w, t.image.h, s0->second.info.format, t.image.px)) {
                            xc.key = key, xc.anchor_x = t.anchor_x, xc.anchor_y = t.anchor_y;
                        }
                    }
                }
                if (m.slots.count(tmp_id) && xc.key == key) {
                    draw_id = tmp_id;
                    blit_x = pos_x + sp.off_x + sp.off2_x - xc.anchor_x;
                    blit_y = pos_y + sp.off_y + sp.off2_y - xc.anchor_y;
                    xformed = true;
                }
            }
        }
        if (xformed) {
        } else if (sp.projected) { // sprite.c mode 5: the picture's origin lands on the projected position, shrunk by the depth
            const std::int32_t k = project_scale(sp.proj_depth(), sp.no_perspective ? 0 : sp.dist);
            const auto scaled = [k](std::int64_t v) { return static_cast<std::int32_t>(v * k >> 16); };
            blit_x = static_cast<std::int32_t>(m.screen.info.width / 2) + (sp.scale_position ? scaled(sp.proj_x()) : sp.proj_x()) - scaled(sp.origin_x) + sp.off_x + sp.off2_x;
            blit_y = static_cast<std::int32_t>(m.screen.info.height / 2) + (sp.scale_position ? scaled(sp.proj_y()) : sp.proj_y()) - scaled(sp.origin_y) + sp.off_y + sp.off2_y;
            auto s0 = m.slots.find(draw_id);
            if (k != 0x10000 && k > 0 && s0 != m.slots.end() && (s0->second.info.format == SlotFormat::Bgra || s0->second.info.format == SlotFormat::Bgrx)) {
                Impl::ScaleCache& sc = m.scales[kv->first];
                const std::uint32_t sw = s0->second.info.width, sh = s0->second.info.height;
                const std::uint32_t tw = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::int64_t{sw} * k >> 16));
                const std::uint32_t th = std::max<std::uint32_t>(1, static_cast<std::uint32_t>(std::int64_t{sh} * k >> 16));
                const std::uint32_t tmp_id = 0x7F300000u | (kv->first & 0xFFFFFu);
                if (sc.src != draw_id || sc.src_px.empty() || sc.scale != k || !m.slots.count(tmp_id)) {
                    if (sc.src != draw_id || sc.src_px.empty()) {
                        if (auto px = m.download(s0->second)) sc.src_px = std::move(*px), sc.src = draw_id;
                    }
                    if (!sc.src_px.empty() && sc.src == draw_id) {
                        const bool has_alpha = s0->second.info.format == SlotFormat::Bgra;
                        std::vector<std::uint32_t> out(std::size_t{tw} * th);
                        for (std::uint32_t y = 0; y < th; ++y) {
                            const double fy = std::clamp((y + 0.5) * sh / th - 0.5, 0.0, sh - 1.0);
                            const std::uint32_t y0 = static_cast<std::uint32_t>(fy), y1 = std::min(y0 + 1, sh - 1);
                            const double wy = fy - y0;
                            for (std::uint32_t x = 0; x < tw; ++x) {
                                const double fx = std::clamp((x + 0.5) * sw / tw - 0.5, 0.0, sw - 1.0);
                                const std::uint32_t x0 = static_cast<std::uint32_t>(fx), x1 = std::min(x0 + 1, sw - 1);
                                const double wx = fx - x0;
                                const std::uint32_t taps[4] = {sc.src_px[std::size_t{y0} * sw + x0], sc.src_px[std::size_t{y0} * sw + x1], sc.src_px[std::size_t{y1} * sw + x0],
                                                               sc.src_px[std::size_t{y1} * sw + x1]};
                                const double wt[4] = {(1 - wx) * (1 - wy), wx * (1 - wy), (1 - wx) * wy, wx * wy};
                                double a = 0, c[3] = {0, 0, 0};
                                for (int i = 0; i < 4; ++i) { // colours weighted by their alpha, so a transparent edge does not bleed
                                    const double ta = has_alpha ? (taps[i] >> 24) : 255.0;
                                    a += wt[i] * ta;
                                    for (int ch = 0; ch < 3; ++ch) c[ch] += wt[i] * ta * ((taps[i] >> (8 * ch)) & 0xFF);
                                }
                                std::uint32_t px = has_alpha ? static_cast<std::uint32_t>(a + 0.5) << 24 : 0;
                                for (int ch = 0; ch < 3; ++ch) px |= static_cast<std::uint32_t>(a > 0 ? c[ch] / a + 0.5 : 0) << (8 * ch);
                                out[std::size_t{y} * tw + x] = px;
                            }
                        }
                        if (upload_slot(tmp_id, tw, th, s0->second.info.format, out)) sc.scale = k;
                    }
                }
                if (m.slots.count(tmp_id) && sc.scale == k) draw_id = tmp_id;
            }
        }
        auto it = m.slots.find(draw_id);
        if (it == m.slots.end()) continue; // a sprite whose image was freed draws nothing
        if (!blend_mode_supported(sp.blend)) return fail(Errc::Unsupported, "blend mode " + std::to_string(sp.blend) + " is not implemented");
        std::uint32_t alpha = sp.alpha;
        if (sp.fade || sp.opacity != 0x100) { // the effective level of the effect family (dispobj.c: DispObj_EffectiveLevel)
            if (sp.blend == 1 || (sp.blend >= 0x20 && sp.blend <= 0x24))
                alpha = 0x100u - (((0x100u - sp.fade) * (0x100u - std::min(alpha, 0x100u)) * sp.opacity) >> 16);
            else if ((sp.blend >= 2 && sp.blend <= 4) || sp.blend == 0xC0 || sp.blend == 0xC1)
                alpha = ((0x100u - sp.fade) * alpha * sp.opacity) >> 16;
        }
        if (sp.ripple_map != -1) alpha = std::min(sp.fade, 0x100u); // Blit_RippleLevel draws with the fade as its level, the amplitude is the level
        if (windows_level && sp.blend == 1 && window_parts.count(kv->first))
            alpha = 0x100u - (((0x100u - std::min(alpha, 0x100u)) * (0x100u - std::min(windows_level, 0x100u))) >> 8);
        if (auto r = m.do_blit(m.screen, blit_x, blit_y, it->second, 0, 0, it->second.info.width, it->second.info.height, sp.blend,
                               alpha);
            !r)
            return r;
    }
    return {};
}

Result<void> Display::snapshot(std::uint32_t id, std::optional<std::uint32_t> max_priority) {
    auto& m = *impl_;
    auto buffer = m.make_buffer(m.screen.info.width * m.screen.info.height);
    if (!buffer) return std::unexpected(buffer.error());
    GpuSlot target{m.screen.info, *buffer, ++m.next_generation};
    Result<void> rendered;
    if (!max_priority) {
        // 90:04 copies the already composed back buffer, including the frame
        // visible before scripts changed or removed any scene objects.
        rendered = m.do_blit(target, 0, 0, m.screen, 0, 0, m.screen.info.width, m.screen.info.height, 0, 0x100);
    } else {
        struct TargetScope {
            GpuSlot& screen;
            GpuSlot& target;
            TargetScope(GpuSlot& screen, GpuSlot& target) : screen(screen), target(target) { std::swap(screen, target); }
            ~TargetScope() { std::swap(screen, target); }
        } scope(m.screen, target);
        rendered = compose(max_priority);
    }
    if (!rendered) { SDL_ReleaseGPUBuffer(m.device, target.buffer); return rendered; }
    if (const auto old = m.slots.find(id); old != m.slots.end()) SDL_ReleaseGPUBuffer(m.device, old->second.buffer);
    m.stale(id);
    m.slots[id] = target;
    return {};
}

std::uint32_t Display::screen_width() const { return impl_->screen.info.width; }
std::uint32_t Display::screen_height() const { return impl_->screen.info.height; }

Result<std::vector<std::uint32_t>> Display::read_screen() { return impl_->download(impl_->screen); }

Result<void> Display::present() {
    Impl& m = *impl_;
    const bool perf = std::getenv("OBGI_PERF") != nullptr;
    const auto perf_start = std::chrono::steady_clock::now();
    if (auto r = compose(); !r) return r;
    const auto compose_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - perf_start).count();
    if (perf && compose_ms >= 20) std::fprintf(stderr, "PERF compose ms=%lld\n", static_cast<long long>(compose_ms));

    auto c = m.ensure_cmd();
    if (!c) return std::unexpected(c.error());
    SDL_GPUStorageTextureReadWriteBinding tex{m.screen_tex->native(), 0, 0, false};
    SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(*c, &tex, 1, nullptr, 0);
    SDL_BindGPUComputePipeline(pass, m.present_pipeline);
    SDL_GPUBuffer* src = m.screen.buffer;
    SDL_BindGPUComputeStorageBuffers(pass, 0, &src, 1);
    PresentParams pp{{m.screen.info.width, m.screen.info.height, static_cast<std::uint32_t>(m.screen.info.format), 0}};
    SDL_PushGPUComputeUniformData(*c, 0, &pp, sizeof pp);
    SDL_DispatchGPUCompute(pass, (pp.info[0] + 7) / 8, (pp.info[1] + 7) / 8, 1);
    SDL_EndGPUComputePass(pass);
    if (auto f = m.flush(false); !f) return f;

    const float black[4] = {0, 0, 0, 1};
    if (auto r = m.renderer->begin_frame(black); !r) return r;
    DrawParams dp;
    dp.x = static_cast<float>(present_dx), dp.y = static_cast<float>(present_dy);
    dp.w = static_cast<float>(m.screen.info.width);
    dp.h = static_cast<float>(m.screen.info.height);
    m.renderer->draw(*m.screen_tex, dp);
    return m.renderer->end_frame();
}

} // namespace obgi
