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
    SDL_GPUBuffer* dummy = nullptr; // bound as the unused read-only buffer of fills
    std::map<std::uint32_t, GpuSlot> slots;
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

const SlotInfo* Display::slot(std::uint32_t id) const {
    auto it = impl_->slots.find(id);
    return it == impl_->slots.end() ? nullptr : &it->second.info;
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

Sprite* Display::find_sprite(std::uint32_t id) {
    // handle 0 is the background object, whose position, level, progress and visibility are the upper picture's
    if (id == 0)
        if (auto bg = sprites_.find(kBackgroundUpper); bg != sprites_.end()) return &bg->second;
    auto it = sprites_.find(id);
    return it == sprites_.end() ? nullptr : &it->second;
}

void Display::set_visible(std::uint32_t id, bool on, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->visible = on;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_visible(kv.first, on, depth + 1);
}

void Display::set_progress(std::uint32_t id, std::uint32_t v, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->progress = v;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_progress(kv.first, v, depth + 1);
}

void Display::set_fade(std::uint32_t id, std::uint32_t v, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->fade = v;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_fade(kv.first, v, depth + 1);
}

void Display::set_opacity(std::uint32_t id, std::uint32_t v, int depth) {
    Sprite* sp = find_sprite(id);
    if (!sp || depth > 16) return;
    sp->opacity = v;
    for (auto& kv : sprites_)
        if (kv.second.has_master && kv.second.master == id) set_opacity(kv.first, v, depth + 1);
}

void Display::resolve_masters() {
    // masters first: a sprite is settled after the one it follows (chains are short; cycles are cut by `done`)
    std::map<std::uint32_t, bool> done;
    auto settle = [&](auto&& self, std::uint32_t id, int depth) -> void {
        Sprite* sp = find_sprite(id);
        if (!sp || done[id]) return;
        done[id] = true;
        if (!sp->has_master || depth > 16) return;
        self(self, sp->master, depth + 1);
        Sprite* ms = find_sprite(sp->master);
        if (!ms) return;
        if (sp->moved) { // moved by the script itself: remember the new offset
            sp->master_dx = sp->x - ms->x;
            sp->master_dy = sp->y - ms->y;
            sp->moved = false;
        }
        sp->x = ms->x + sp->master_dx;
        sp->y = ms->y + sp->master_dy;
    };
    for (auto& kv : sprites_) settle(settle, kv.first, 0);
}

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
    std::vector<std::int64_t> key = {bg_fx.type, std::int64_t(bg_fx.revision), level, px, py, up->fade, up->opacity, w, h};
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
    case 7: {
        if (const bgfx::Image* src = image(bg_fx.bmp[0], true)) out = bgfx::box_blur(*src, bg_fx.edge, level);
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

Result<void> Display::compose() {
    Impl& m = *impl_;
    if (auto r = m.fill(m.screen, 0); !r) return r;
    resolve_masters();
    if (auto r = render_background_fx(); !r) return r;
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
        for (const auto& kv : sprites_)
            if (in_window(kv.first)) window_parts.insert(kv.first);
    for (const auto& kv : sprites_)
        if (kv.second.is_shown() && (kv.second.priority >= draw_limit || kv.second.background) &&
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
        // a projected plane is ordered by depth, a larger z (farther away) sorting lower; the others by creation order
        if (sp.projected) return hi + ((0xffff8000u - static_cast<std::uint32_t>(static_cast<std::uint16_t>(sp.depth >> 16))) & 0xffffu);
        const auto cr = created_.find(kv->first);
        return hi + (cr == created_.end() ? 0 : cr->second & 0x7FFF);
    };
    std::stable_sort(order.begin(), order.end(), [&](auto* a, auto* b) { return sort_key(a) < sort_key(b); });
    for (const auto* kv : order) {
        const Sprite& sp = kv->second;
        if (std::getenv("OBGI_SPRITES")) std::fprintf(stderr, "sprite %X slot %u at %d,%d blend %u alpha %u prio %u%s""\n", kv->first, sp.slot, sp.x, sp.y, sp.blend, sp.alpha, sp.priority, m.slots.count(sp.slot) ? "" : " (no slot)");
        if (sp.filter_kind >= 0) { // kind 0: the picture so far is blended towards the colour by level / 256 (blit.c: Tint32); kinds 1..3 are not drawn yet
            if (sp.filter_kind != 0 || sp.alpha == 0) continue;
            constexpr std::uint32_t kFilterSlot = 0x7F200000u;
            const std::uint32_t fw = m.screen.info.width, fh = m.screen.info.height;
            const SlotInfo* fs = slot(kFilterSlot);
            if (!fs || fs->width != fw || fs->height != fh) {
                if (auto made = create_slot(kFilterSlot, fw, fh, SlotFormat::Bgrx); !made) return made;
            }
            if (auto filled = fill_slot(kFilterSlot, sp.filter_colour & 0xFFFFFFu); !filled) return filled;
            auto fit = m.slots.find(kFilterSlot);
            if (auto r = m.do_blit(m.screen, 0, 0, fit->second, 0, 0, fw, fh, 1, 0x100u - std::min(sp.alpha, 0x100u)); !r) return r;
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
        if (sp.projected) { // sprite.c mode 5: the picture's origin lands on the projected position, shrunk by the depth
            const std::int32_t k = project_scale(sp.depth, sp.no_perspective ? 0 : sp.dist);
            const auto scaled = [k](std::int64_t v) { return static_cast<std::int32_t>(v * k >> 16); };
            blit_x = static_cast<std::int32_t>(m.screen.info.width / 2) + (sp.scale_position ? scaled(sp.x) : sp.x) - scaled(sp.origin_x) / 2 + sp.off_x + sp.off2_x;
            blit_y = static_cast<std::int32_t>(m.screen.info.height / 2) + (sp.scale_position ? scaled(sp.y) : sp.y) - scaled(sp.origin_y) / 2 + sp.off_y + sp.off2_y;
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
        if (windows_level && sp.blend == 1 && window_parts.count(kv->first))
            alpha = 0x100u - (((0x100u - std::min(alpha, 0x100u)) * (0x100u - std::min(windows_level, 0x100u))) >> 8);
        if (auto r = m.do_blit(m.screen, blit_x, blit_y, it->second, 0, 0, it->second.info.width, it->second.info.height, sp.blend,
                               alpha);
            !r)
            return r;
    }
    return {};
}

std::uint32_t Display::screen_width() const { return impl_->screen.info.width; }
std::uint32_t Display::screen_height() const { return impl_->screen.info.height; }

Result<std::vector<std::uint32_t>> Display::read_screen() { return impl_->download(impl_->screen); }

Result<void> Display::present() {
    Impl& m = *impl_;
    if (auto r = compose(); !r) return r;

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
    dp.w = static_cast<float>(m.screen.info.width);
    dp.h = static_cast<float>(m.screen.info.height);
    m.renderer->draw(*m.screen_tex, dp);
    return m.renderer->end_frame();
}

} // namespace obgi
