#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/error.hpp"

namespace obgi {

// One blend evaluation: pixels are packed as in memory (see shaders/blend.glsl).
struct BlendCase {
    std::uint8_t mode = 0;
    std::uint8_t src_format = 0, dst_format = 0; // 0 = 16-bit, 1 = 32-bit, 2 = 32-bit + alpha, 3 = 8-bit
    std::uint16_t p = 0;
    std::uint32_t src = 0, dst = 0;
};

// Headless GPU context that evaluates the blend kernels in a compute shader. It exists to prove that the
// shader math reproduces the original CPU kernels; the renderer reuses the same shader library.
class GpuBlendRunner {
  public:
    // `driver` forces an SDL_GPU backend ("vulkan", "direct3d12", "metal"); nullptr lets SDL pick.
    static Result<std::unique_ptr<GpuBlendRunner>> create(const char* driver = nullptr);
    ~GpuBlendRunner();
    GpuBlendRunner(const GpuBlendRunner&) = delete;
    GpuBlendRunner& operator=(const GpuBlendRunner&) = delete;

    Result<std::vector<std::uint32_t>> run(std::span<const BlendCase> cases);
    const char* driver_name() const;

  private:
    GpuBlendRunner() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace obgi
