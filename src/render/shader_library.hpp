#pragma once
#include <filesystem>
#include <string>

#include "render/shader_pack.hpp"
#include "vfs/mapped_file.hpp"

namespace obgi {

// A shader pack loaded from disk. Lookup order for <name>.pack:
//   1. explicit override_dir (if non-empty)  2. $OBGI_SHADER_DIR  3. <executable dir>/shaders  4. the build tree
class ShaderLibrary {
  public:
    static Result<ShaderLibrary> open(const std::string& name, const std::filesystem::path& override_dir = {});

    const ShaderPack& pack() const { return pack_; }

  private:
    ShaderLibrary(MappedFile file, ShaderPack pack) : file_(std::move(file)), pack_(pack) {}

    MappedFile file_;
    ShaderPack pack_;
};

} // namespace obgi
