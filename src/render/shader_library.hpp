#pragma once
#include <string>

#include "render/shader_pack.hpp"
#include "vfs/mapped_file.hpp"

namespace obgi {

// A shader pack loaded from disk. Lookup order for <name>.pack:
//   1. $OBGI_SHADER_DIR  2. <executable dir>/shaders  3. the build tree (development fallback)
class ShaderLibrary {
  public:
    static Result<ShaderLibrary> open(const std::string& name);

    const ShaderPack& pack() const { return pack_; }

  private:
    ShaderLibrary(MappedFile file, ShaderPack pack) : file_(std::move(file)), pack_(pack) {}

    MappedFile file_;
    ShaderPack pack_;
};

} // namespace obgi
