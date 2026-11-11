#pragma once
#include <chrono>
#include <functional>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/bytes.hpp"
#include "core/error.hpp"
#include "render/shader_pack.hpp"

struct SDL_GPUDevice;
struct SDL_GPUTexture;

namespace obgi {

enum class PixelFormat { Rgba8, Bgra8 };

struct DrawParams {
    // Destination rectangle in logical canvas pixels (origin top-left).
    float x = 0, y = 0, w = 0, h = 0;
    // Source rectangle in texels; w or h of 0 means the whole texture.
    float sx = 0, sy = 0, sw = 0, sh = 0;
    float alpha = 1.0f; // 0..1, straight alpha over the target
};

// A GPU texture owned by the Renderer that made it; destroy it before the Renderer.
class Texture {
  public:
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    std::uint32_t width() const { return width_; }
    std::uint32_t height() const { return height_; }
    SDL_GPUTexture* native() const;

  private:
    friend class Renderer;
    Texture() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t width_ = 0, height_ = 0;
};

struct RendererOptions {
    const char* title = "OpenBGI";
    std::uint32_t logical_width = 800;  // the game's virtual screen
    std::uint32_t logical_height = 600;
    bool headless = false;              // render into an offscreen image instead of a window
    const char* driver = nullptr;       // "vulkan", "direct3d12", "metal"; null = SDL's choice
};

struct InputPress {
    std::uint32_t key;
    int x = 0, y = 0;
    bool pointer = false;
};

// Presents textured, alpha-blended quads on a fixed logical canvas, letterboxed into the window.
// Blend modes other than plain alpha belong to the compute path (render/gpu_blend), not here.
class Renderer {
  public:
    static Result<std::unique_ptr<Renderer>> create(const RendererOptions& options);
    ~Renderer();

    const char* driver_name() const;
    std::uint32_t logical_width() const;
    std::uint32_t logical_height() const;
    std::uint32_t presentation_interval_us() const;
    // Request activation once the interactive session is ready.
    void raise_window();
    Result<void> resize_canvas(std::uint32_t width, std::uint32_t height);

    // For compute-side users (render/display): the device and the packed shaders of this renderer.
    SDL_GPUDevice* native_device() const;
    const ShaderPack& shaders() const;

    // An RGBA8 texture compute shaders can write (storage image) and draw() can sample.
    Result<std::unique_ptr<Texture>> create_target_texture(std::uint32_t w, std::uint32_t h);

    Result<std::unique_ptr<Texture>> create_texture(std::uint32_t w, std::uint32_t h, PixelFormat format, ByteView pixels);
    Result<void> update_texture(Texture& texture, ByteView pixels);

    // Returns false when the window was closed (or asked to quit); pumps SDL events.
    bool poll_events();
    // Wait without removing the next SDL event, so poll_events owns dispatch.
    void wait_for_event(std::chrono::steady_clock::time_point deadline);
    std::function<void()> completion_notifier() const;
    // Last key pressed since the previous call, as an SDL keycode (0 = none).
    std::uint32_t take_key();
    // Key and mouse-button presses since the previous call as Windows virtual-key codes (mouse: 1 left, 2 right, 4 middle,
    // 5, 6); keyboard auto-repeat is ignored until the key is released, while each mouse click counts.
    std::vector<InputPress> take_presses();
    // Current held state indexed by Windows virtual-key code.
    std::array<bool, 256> held_inputs() const;
    // The pointer in logical canvas coordinates (outside the canvas when it is in the letterbox bars).
    std::pair<int, int> mouse_position() const;
    bool left_button_down() const; // the left mouse button is held
    int take_wheel();              // wheel notches since the last call (positive = away from the user)
    std::vector<int> take_wheel_events(); // each event as +1 away / -1 toward, preserving order

    Result<void> begin_frame(const float clear_rgba[4]);
    // Returns false without blocking when the window swapchain has no free image.
    Result<bool> try_begin_frame(const float clear_rgba[4]);
    void draw(const Texture& tex, const DrawParams& p);
    Result<void> end_frame();

    // Headless only: RGBA8 of the last finished frame, logical_width * logical_height * 4.
    Result<std::vector<std::uint8_t>> read_back();

  private:
    Result<bool> begin_frame_impl(const float clear_rgba[4], bool wait);
    Renderer();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace obgi
