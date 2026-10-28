#pragma once
#include <cstdint>
#include <optional>
#include <string_view>

#include "core/bytes.hpp"
#include "core/error.hpp"

// A shader pack is the one compiled-shader file produced per platform at build time.
//   header  : "OBSP", version u32, entry count u32, reserved u32
//   entries : name[32], entry point[16], format u32 (an SDL_GPUShaderFormat flag), offset u32, size u32, pad u32
//   blobs   : raw SPIR-V / DXIL / metallib bytes; entries may share one blob (a metallib holds every shader)
namespace obgi {

namespace shader_pack {
constexpr char kMagic[4] = {'O', 'B', 'S', 'P'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kEntrySize = 64;
constexpr std::size_t kNameSize = 32;
constexpr std::size_t kEntryPointSize = 16;
// Same values as SDL_GPU_SHADERFORMAT_*; kept literal so the packer tool does not need SDL.
constexpr std::uint32_t kSpirv = 1u << 1, kDxbc = 1u << 2, kDxil = 1u << 3, kMsl = 1u << 4, kMetallib = 1u << 5;
} // namespace shader_pack

struct ShaderBlob {
    std::uint32_t format = 0;
    std::string_view entry_point;
    ByteView data;
};

class ShaderPack {
  public:
    // `pack` must outlive the ShaderPack.
    static Result<ShaderPack> parse(ByteView pack);

    // First entry called `name` whose format is in `supported_formats` (a bit mask).
    std::optional<ShaderBlob> find(std::string_view name, std::uint32_t supported_formats) const;

  private:
    ByteView data_;
    std::uint32_t count_ = 0;
};

} // namespace obgi
