#include <cstdio>
#include <cstdlib>
#include "render/renderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <utility>

#include "render/shader_library.hpp"

namespace obgi {

namespace {
Error sdl_error(const char* what) { return Error{Errc::Io, std::string(what) + ": " + SDL_GetError()}; }

struct BlitUniform {
    float rect[4];
    float uv_rect[4];
    float tint[4];
};
static_assert(sizeof(BlitUniform) == 48);
} // namespace

struct Texture::Impl {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUTexture* texture = nullptr;
};

SDL_GPUTexture* Texture::native() const { return impl_->texture; }

Texture::~Texture() {
    if (impl_ && impl_->texture) SDL_ReleaseGPUTexture(impl_->device, impl_->texture);
}

struct Renderer::Impl {
    RendererOptions opt;
    SDL_Window* window = nullptr;
    SDL_GPUDevice* device = nullptr;
    SDL_GPUGraphicsPipeline* pipeline = nullptr;
    SDL_GPUSampler* sampler = nullptr;
    SDL_GPUTexture* offscreen = nullptr; // headless target
    SDL_GPUTextureFormat target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    bool video_inited = false;
    std::optional<ShaderLibrary> library;

    SDL_GPUCommandBuffer* cmd = nullptr;
    SDL_GPURenderPass* pass = nullptr;
    bool frame_skipped = false;
    std::vector<std::uint8_t> last_frame;
    std::uint32_t key = 0;
    std::vector<std::uint32_t> presses;
    int mouse_x = 0, mouse_y = 0; // pointer in logical canvas coordinates
    bool left_down = false;
    int wheel = 0;

    ~Impl() {
        if (pipeline) SDL_ReleaseGPUGraphicsPipeline(device, pipeline);
        if (sampler) SDL_ReleaseGPUSampler(device, sampler);
        if (offscreen) SDL_ReleaseGPUTexture(device, offscreen);
        if (window && device) SDL_ReleaseWindowFromGPUDevice(device, window);
        if (window) SDL_DestroyWindow(window);
        if (device) SDL_DestroyGPUDevice(device);
        if (video_inited) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

SDL_GPUDevice* Renderer::native_device() const { return impl_->device; }
const ShaderPack& Renderer::shaders() const { return impl_->library->pack(); }

const char* Renderer::driver_name() const { return SDL_GetGPUDeviceDriver(impl_->device); }

Result<std::unique_ptr<Renderer>> Renderer::create(const RendererOptions& options) {
    std::unique_ptr<Renderer> r(new Renderer);
    Impl& m = *r->impl_;
    m.opt = options;

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return std::unexpected(sdl_error("SDL_Init"));
    m.video_inited = true;
    constexpr SDL_GPUShaderFormat kFormats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                             SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
    m.device = SDL_CreateGPUDevice(kFormats, false, options.driver);
    if (!m.device) return std::unexpected(sdl_error("SDL_CreateGPUDevice"));

    if (options.headless) {
        SDL_GPUTextureCreateInfo ti{};
        ti.type = SDL_GPU_TEXTURETYPE_2D;
        ti.format = m.target_format;
        ti.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
        ti.width = options.logical_width;
        ti.height = options.logical_height;
        ti.layer_count_or_depth = 1;
        ti.num_levels = 1;
        m.offscreen = SDL_CreateGPUTexture(m.device, &ti);
        if (!m.offscreen) return std::unexpected(sdl_error("offscreen target"));
    } else {
        m.window = SDL_CreateWindow(options.title, int(options.logical_width), int(options.logical_height),
                                    SDL_WINDOW_RESIZABLE);
        if (!m.window) return std::unexpected(sdl_error("SDL_CreateWindow"));
        if (!SDL_ClaimWindowForGPUDevice(m.device, m.window)) return std::unexpected(sdl_error("claim window"));
        m.target_format = SDL_GetGPUSwapchainTextureFormat(m.device, m.window);
    }

    auto opened = ShaderLibrary::open("obgi");
    if (!opened) return std::unexpected(opened.error());
    m.library.emplace(std::move(*opened));
    auto* library = &*m.library;
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(m.device);
    auto make_shader = [&](const char* name, SDL_GPUShaderStage stage) -> Result<SDL_GPUShader*> {
        auto blob = library->pack().find(name, supported);
        if (!blob) return fail(Errc::Unsupported, std::string("no ") + name + " shader for the " + SDL_GetGPUDeviceDriver(m.device) + " backend");
        const std::string entry(blob->entry_point);
        SDL_GPUShaderCreateInfo si{};
        si.code = blob->data.data();
        si.code_size = blob->data.size();
        si.entrypoint = entry.c_str();
        si.format = static_cast<SDL_GPUShaderFormat>(blob->format);
        si.stage = stage;
        si.num_samplers = stage == SDL_GPU_SHADERSTAGE_FRAGMENT ? 1 : 0;
        si.num_uniform_buffers = stage == SDL_GPU_SHADERSTAGE_VERTEX ? 1 : 0;
        SDL_GPUShader* s = SDL_CreateGPUShader(m.device, &si);
        if (!s) return std::unexpected(sdl_error(name));
        return s;
    };
    auto vs = make_shader("blit_vert", SDL_GPU_SHADERSTAGE_VERTEX);
    if (!vs) return std::unexpected(vs.error());
    auto fs = make_shader("blit_frag", SDL_GPU_SHADERSTAGE_FRAGMENT);
    if (!fs) {
        SDL_ReleaseGPUShader(m.device, *vs);
        return std::unexpected(fs.error());
    }

    SDL_GPUColorTargetDescription target{};
    target.format = m.target_format;
    target.blend_state.enable_blend = true;
    target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo pi{};
    pi.vertex_shader = *vs;
    pi.fragment_shader = *fs;
    pi.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
    pi.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    pi.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    pi.target_info.color_target_descriptions = &target;
    pi.target_info.num_color_targets = 1;
    m.pipeline = SDL_CreateGPUGraphicsPipeline(m.device, &pi);
    SDL_ReleaseGPUShader(m.device, *vs);
    SDL_ReleaseGPUShader(m.device, *fs);
    if (!m.pipeline) return std::unexpected(sdl_error("SDL_CreateGPUGraphicsPipeline"));

    SDL_GPUSamplerCreateInfo sci{};
    sci.min_filter = SDL_GPU_FILTER_LINEAR;
    sci.mag_filter = SDL_GPU_FILTER_LINEAR;
    sci.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    sci.address_mode_u = sci.address_mode_v = sci.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m.sampler = SDL_CreateGPUSampler(m.device, &sci);
    if (!m.sampler) return std::unexpected(sdl_error("SDL_CreateGPUSampler"));
    return r;
}

Result<std::unique_ptr<Texture>> Renderer::create_target_texture(std::uint32_t w, std::uint32_t h) {
    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    SDL_GPUTexture* gpu = SDL_CreateGPUTexture(impl_->device, &ti);
    if (!gpu) return std::unexpected(sdl_error("SDL_CreateGPUTexture (target)"));
    std::unique_ptr<Texture> t(new Texture);
    t->impl_ = std::make_unique<Texture::Impl>();
    t->impl_->device = impl_->device;
    t->impl_->texture = gpu;
    t->width_ = w;
    t->height_ = h;
    return t;
}

Result<std::unique_ptr<Texture>> Renderer::create_texture(std::uint32_t w, std::uint32_t h, PixelFormat format,
                                                          ByteView pixels) {
    Impl& m = *impl_;
    const std::size_t bytes = std::size_t{w} * h * 4;
    if (w == 0 || h == 0 || pixels.size() < bytes) return fail(Errc::Range, "texture pixel data too small");

    SDL_GPUTextureCreateInfo ti{};
    ti.type = SDL_GPU_TEXTURETYPE_2D;
    ti.format = format == PixelFormat::Rgba8 ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM : SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
    ti.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    ti.width = w;
    ti.height = h;
    ti.layer_count_or_depth = 1;
    ti.num_levels = 1;
    SDL_GPUTexture* gpu = SDL_CreateGPUTexture(m.device, &ti);
    if (!gpu) return std::unexpected(sdl_error("SDL_CreateGPUTexture"));

    SDL_GPUTransferBufferCreateInfo bi{};
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    bi.size = static_cast<Uint32>(bytes);
    SDL_GPUTransferBuffer* up = SDL_CreateGPUTransferBuffer(m.device, &bi);
    if (!up) {
        SDL_ReleaseGPUTexture(m.device, gpu);
        return std::unexpected(sdl_error("transfer buffer"));
    }
    std::memcpy(SDL_MapGPUTransferBuffer(m.device, up, false), pixels.data(), bytes);
    SDL_UnmapGPUTransferBuffer(m.device, up);

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m.device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src{up, 0, 0, 0};
    SDL_GPUTextureRegion dst{};
    dst.texture = gpu;
    dst.w = w;
    dst.h = h;
    dst.d = 1;
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    if (fence) {
        SDL_WaitForGPUFences(m.device, true, &fence, 1);
        SDL_ReleaseGPUFence(m.device, fence);
    }
    SDL_ReleaseGPUTransferBuffer(m.device, up);
    if (!fence) {
        SDL_ReleaseGPUTexture(m.device, gpu);
        return std::unexpected(sdl_error("texture upload"));
    }

    std::unique_ptr<Texture> t(new Texture);
    t->impl_ = std::make_unique<Texture::Impl>();
    t->impl_->device = m.device;
    t->impl_->texture = gpu;
    t->width_ = w;
    t->height_ = h;
    return t;
}

namespace {

// SDL keycode -> Windows virtual-key code (0 = not one the game uses)
std::uint32_t virtual_key(SDL_Keycode k) {
    if (k >= SDLK_0 && k <= SDLK_9) return 0x30 + (k - SDLK_0);
    if (k >= SDLK_A && k <= SDLK_Z) return 0x41 + (k - SDLK_A);
    switch (k) {
    case SDLK_RETURN: case SDLK_KP_ENTER: return 0x0D;
    case SDLK_SPACE: return 0x20;
    case SDLK_TAB: return 0x09;
    case SDLK_ESCAPE: return 0x1B;
    case SDLK_LEFT: return 0x25;
    case SDLK_UP: return 0x26;
    case SDLK_RIGHT: return 0x27;
    case SDLK_DOWN: return 0x28;
    case SDLK_LCTRL: case SDLK_RCTRL: return 0x11;
    default: return 0;
    }
}

} // namespace

bool Renderer::poll_events() {
    bool alive = true;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) { alive = false; if (std::getenv("OBGI_DBGWIN")) std::fprintf(stderr, "window event %u\n", e.type); }
        if (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat) impl_->key = e.key.key;
        if (e.type == SDL_EVENT_KEY_DOWN)
            if (const std::uint32_t vk = virtual_key(e.key.key)) impl_->presses.push_back(vk);
        if (e.type == SDL_EVENT_MOUSE_MOTION || e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            // window units -> pixels -> the letterboxed logical canvas
            const float mx = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.x : e.button.x;
            const float my = e.type == SDL_EVENT_MOUSE_MOTION ? e.motion.y : e.button.y;
            int ww = 0, wh = 0, pw = 0, ph = 0;
            if (impl_->window && SDL_GetWindowSize(impl_->window, &ww, &wh) && SDL_GetWindowSizeInPixels(impl_->window, &pw, &ph) && ww > 0 && wh > 0) {
                const float lw = float(impl_->opt.logical_width), lh = float(impl_->opt.logical_height);
                const float scale = std::min(float(pw) / lw, float(ph) / lh);
                impl_->mouse_x = int((mx * pw / ww - (pw - lw * scale) * 0.5f) / scale);
                impl_->mouse_y = int((my * ph / wh - (ph - lh * scale) * 0.5f) / scale);
            }
        }
        if ((e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) && e.button.button == 1) impl_->left_down = e.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            // one notch per event, like WM_MOUSEWHEEL (the engine looks only at the sign); precise touchpads send fractions
            if (e.wheel.y != 0) impl_->wheel += e.wheel.y > 0 ? 1 : -1;
        }
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            static constexpr std::uint32_t kButtons[] = {0, 1, 4, 2, 5, 6}; // SDL: 1 left, 2 middle, 3 right, 4 X1, 5 X2
            if (e.button.button < 6) impl_->presses.push_back(kButtons[e.button.button]);
        }
    }
    return alive;
}

std::vector<std::uint32_t> Renderer::take_presses() { return std::exchange(impl_->presses, {}); }

std::pair<int, int> Renderer::mouse_position() const { return {impl_->mouse_x, impl_->mouse_y}; }

bool Renderer::left_button_down() const { return impl_->left_down; }

int Renderer::take_wheel() { return std::exchange(impl_->wheel, 0); }

std::uint32_t Renderer::take_key() { return std::exchange(impl_->key, 0u); }

Result<void> Renderer::begin_frame(const float clear[4]) {
    Impl& m = *impl_;
    m.cmd = SDL_AcquireGPUCommandBuffer(m.device);
    if (!m.cmd) return std::unexpected(sdl_error("acquire command buffer"));
    m.frame_skipped = false;

    SDL_GPUTexture* target = m.offscreen;
    Uint32 tw = m.opt.logical_width, th = m.opt.logical_height;
    if (m.window) {
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(m.cmd, m.window, &target, &tw, &th))
            return std::unexpected(sdl_error("acquire swapchain"));
        if (!target) { // minimized: nothing to draw this frame
            m.frame_skipped = true;
            return {};
        }
    }

    SDL_GPUColorTargetInfo ci{};
    ci.texture = target;
    ci.clear_color = SDL_FColor{clear[0], clear[1], clear[2], clear[3]};
    ci.load_op = SDL_GPU_LOADOP_CLEAR;
    ci.store_op = SDL_GPU_STOREOP_STORE;
    m.pass = SDL_BeginGPURenderPass(m.cmd, &ci, 1, nullptr);
    SDL_BindGPUGraphicsPipeline(m.pass, m.pipeline);

    // Letterbox the logical canvas into the target.
    const float scale = std::min(float(tw) / m.opt.logical_width, float(th) / m.opt.logical_height);
    const float vw = m.opt.logical_width * scale, vh = m.opt.logical_height * scale;
    SDL_GPUViewport vp{(tw - vw) * 0.5f, (th - vh) * 0.5f, vw, vh, 0.0f, 1.0f};
    SDL_SetGPUViewport(m.pass, &vp);
    return {};
}

void Renderer::draw(const Texture& tex, const DrawParams& p) {
    Impl& m = *impl_;
    if (!m.pass) return;
    const float lw = float(m.opt.logical_width), lh = float(m.opt.logical_height);
    const float tw = float(tex.width()), th = float(tex.height());
    const float sw = p.sw > 0 ? p.sw : tw - p.sx, sh = p.sh > 0 ? p.sh : th - p.sy;

    BlitUniform u{};
    u.rect[0] = 2.0f * p.x / lw - 1.0f;
    u.rect[1] = 1.0f - 2.0f * p.y / lh;
    u.rect[2] = 2.0f * (p.x + p.w) / lw - 1.0f;
    u.rect[3] = 1.0f - 2.0f * (p.y + p.h) / lh;
    u.uv_rect[0] = p.sx / tw;
    u.uv_rect[1] = p.sy / th;
    u.uv_rect[2] = (p.sx + sw) / tw;
    u.uv_rect[3] = (p.sy + sh) / th;
    u.tint[0] = u.tint[1] = u.tint[2] = 1.0f;
    u.tint[3] = std::clamp(p.alpha, 0.0f, 1.0f);

    SDL_PushGPUVertexUniformData(m.cmd, 0, &u, sizeof u);
    SDL_GPUTextureSamplerBinding b{tex.impl_->texture, m.sampler};
    SDL_BindGPUFragmentSamplers(m.pass, 0, &b, 1);
    SDL_DrawGPUPrimitives(m.pass, 4, 1, 0, 0);
}

Result<void> Renderer::end_frame() {
    Impl& m = *impl_;
    if (m.pass) {
        SDL_EndGPURenderPass(m.pass);
        m.pass = nullptr;
    }
    if (!m.opt.headless || m.frame_skipped) {
        if (!SDL_SubmitGPUCommandBuffer(m.cmd)) return std::unexpected(sdl_error("submit"));
        m.cmd = nullptr;
        return {};
    }

    // Headless: copy the target into a CPU-visible buffer inside the same submission.
    const Uint32 bytes = m.opt.logical_width * m.opt.logical_height * 4;
    SDL_GPUTransferBufferCreateInfo bi{};
    bi.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    bi.size = bytes;
    SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(m.device, &bi);
    if (!down) {
        SDL_CancelGPUCommandBuffer(m.cmd);
        m.cmd = nullptr;
        return std::unexpected(sdl_error("download buffer"));
    }
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(m.cmd);
    SDL_GPUTextureRegion src{};
    src.texture = m.offscreen;
    src.w = m.opt.logical_width;
    src.h = m.opt.logical_height;
    src.d = 1;
    SDL_GPUTextureTransferInfo dst{down, 0, 0, 0};
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(m.cmd);
    m.cmd = nullptr;
    if (!fence) {
        SDL_ReleaseGPUTransferBuffer(m.device, down);
        return std::unexpected(sdl_error("submit"));
    }
    SDL_WaitForGPUFences(m.device, true, &fence, 1);
    SDL_ReleaseGPUFence(m.device, fence);
    const auto* data = static_cast<const std::uint8_t*>(SDL_MapGPUTransferBuffer(m.device, down, false));
    m.last_frame.assign(data, data + bytes);
    SDL_UnmapGPUTransferBuffer(m.device, down);
    SDL_ReleaseGPUTransferBuffer(m.device, down);
    return {};
}

Result<std::vector<std::uint8_t>> Renderer::read_back() {
    if (!impl_->opt.headless) return fail(Errc::Unsupported, "read_back needs a headless renderer");
    return impl_->last_frame;
}

} // namespace obgi
