#include "render/gpu_blend.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "render/shader_library.hpp"

namespace obgi {

namespace {
Error sdl_error(const char* what) { return Error{Errc::Io, std::string(what) + ": " + SDL_GetError()}; }
} // namespace

struct GpuBlendRunner::Impl {
    SDL_GPUDevice* device = nullptr;
    SDL_GPUComputePipeline* pipeline = nullptr;
    bool video_inited = false;

    ~Impl() {
        if (pipeline) SDL_ReleaseGPUComputePipeline(device, pipeline);
        if (device) SDL_DestroyGPUDevice(device);
        if (video_inited) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

GpuBlendRunner::~GpuBlendRunner() = default;

const char* GpuBlendRunner::driver_name() const { return SDL_GetGPUDeviceDriver(impl_->device); }

Result<std::unique_ptr<GpuBlendRunner>> GpuBlendRunner::create(const char* driver) {
    std::unique_ptr<GpuBlendRunner> r(new GpuBlendRunner);
    r->impl_ = std::make_unique<Impl>();
    Impl& m = *r->impl_;

    // The Vulkan backend loads its library through the video subsystem; no window is created.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) return std::unexpected(sdl_error("SDL_Init"));
    m.video_inited = true;
    constexpr SDL_GPUShaderFormat kFormats = SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL |
                                             SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_METALLIB;
    m.device = SDL_CreateGPUDevice(kFormats, false, driver);
    if (!m.device) return std::unexpected(sdl_error("SDL_CreateGPUDevice"));

    // Whatever the platform's pack holds for the format the chosen backend accepts.
    auto library = ShaderLibrary::open("obgi");
    if (!library) return std::unexpected(library.error());
    auto blob = library->pack().find("blend_cases", SDL_GetGPUShaderFormats(m.device));
    if (!blob) return fail(Errc::Unsupported, std::string("no blend shader for the ") + SDL_GetGPUDeviceDriver(m.device) + " backend");
    const std::string entry(blob->entry_point);

    SDL_GPUComputePipelineCreateInfo info{};
    info.code = blob->data.data();
    info.code_size = blob->data.size();
    info.entrypoint = entry.c_str();
    info.format = static_cast<SDL_GPUShaderFormat>(blob->format);
    info.num_readonly_storage_buffers = 1;
    info.num_readwrite_storage_buffers = 1;
    info.num_uniform_buffers = 1;
    info.threadcount_x = 64;
    info.threadcount_y = 1;
    info.threadcount_z = 1;
    m.pipeline = SDL_CreateGPUComputePipeline(m.device, &info);
    if (!m.pipeline) return std::unexpected(sdl_error("SDL_CreateGPUComputePipeline"));
    return r;
}

Result<std::vector<std::uint32_t>> GpuBlendRunner::run(std::span<const BlendCase> cases) {
    Impl& m = *impl_;
    std::vector<std::uint32_t> out;
    out.reserve(cases.size());

    constexpr std::size_t kChunk = 64 * 4096; // cases per dispatch
    for (std::size_t base = 0; base < cases.size(); base += kChunk) {
        const std::uint32_t n = static_cast<std::uint32_t>(std::min(kChunk, cases.size() - base));
        const Uint32 in_bytes = n * 16, out_bytes = n * 4;

        SDL_GPUBufferCreateInfo bi{};
        bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        bi.size = in_bytes;
        SDL_GPUBuffer* in_buf = SDL_CreateGPUBuffer(m.device, &bi);
        bi.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
        bi.size = out_bytes;
        SDL_GPUBuffer* out_buf = SDL_CreateGPUBuffer(m.device, &bi);
        SDL_GPUTransferBufferCreateInfo ti{};
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        ti.size = in_bytes;
        SDL_GPUTransferBuffer* up = SDL_CreateGPUTransferBuffer(m.device, &ti);
        ti.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        ti.size = out_bytes;
        SDL_GPUTransferBuffer* down = SDL_CreateGPUTransferBuffer(m.device, &ti);
        auto cleanup = [&] {
            if (in_buf) SDL_ReleaseGPUBuffer(m.device, in_buf);
            if (out_buf) SDL_ReleaseGPUBuffer(m.device, out_buf);
            if (up) SDL_ReleaseGPUTransferBuffer(m.device, up);
            if (down) SDL_ReleaseGPUTransferBuffer(m.device, down);
        };
        if (!in_buf || !out_buf || !up || !down) {
            Error e = sdl_error("GPU buffer allocation");
            cleanup();
            return std::unexpected(e);
        }

        auto* dst = static_cast<std::uint32_t*>(SDL_MapGPUTransferBuffer(m.device, up, false));
        for (std::uint32_t i = 0; i < n; ++i) {
            const BlendCase& c = cases[base + i];
            dst[4 * i + 0] = c.mode | (c.src_format << 8u) | (c.dst_format << 10u) | (std::uint32_t{c.p} << 16u);
            dst[4 * i + 1] = c.src;
            dst[4 * i + 2] = c.dst;
            dst[4 * i + 3] = 0;
        }
        SDL_UnmapGPUTransferBuffer(m.device, up);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(m.device);
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation loc{up, 0};
        SDL_GPUBufferRegion region{in_buf, 0, in_bytes};
        SDL_UploadToGPUBuffer(copy, &loc, &region, false);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUStorageBufferReadWriteBinding rw{out_buf, false};
        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(cmd, nullptr, 0, &rw, 1);
        SDL_BindGPUComputePipeline(pass, m.pipeline);
        SDL_GPUBuffer* ro = in_buf;
        SDL_BindGPUComputeStorageBuffers(pass, 0, &ro, 1);
        SDL_PushGPUComputeUniformData(cmd, 0, &n, sizeof n);
        SDL_DispatchGPUCompute(pass, (n + 63) / 64, 1, 1);
        SDL_EndGPUComputePass(pass);

        copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUBufferRegion from{out_buf, 0, out_bytes};
        SDL_GPUTransferBufferLocation to{down, 0};
        SDL_DownloadFromGPUBuffer(copy, &from, &to);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fence) {
            Error e = sdl_error("submit");
            cleanup();
            return std::unexpected(e);
        }
        SDL_WaitForGPUFences(m.device, true, &fence, 1);
        SDL_ReleaseGPUFence(m.device, fence);

        const auto* res = static_cast<const std::uint32_t*>(SDL_MapGPUTransferBuffer(m.device, down, false));
        out.insert(out.end(), res, res + n);
        SDL_UnmapGPUTransferBuffer(m.device, down);
        cleanup();
    }
    return out;
}

} // namespace obgi
