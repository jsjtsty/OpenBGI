#include "render/shader_library.hpp"

#include <SDL3/SDL.h>

#include <cstdlib>
#include <filesystem>
#include <vector>

namespace obgi {

Result<ShaderLibrary> ShaderLibrary::open(const std::string& name) {
    namespace fs = std::filesystem;
    std::vector<fs::path> dirs;
    if (const char* env = std::getenv("OBGI_SHADER_DIR")) dirs.emplace_back(env);
    if (const char* base = SDL_GetBasePath()) dirs.push_back(fs::path(base) / "shaders");
    dirs.emplace_back(OBGI_BUILD_SHADER_DIR);

    std::string tried;
    for (const auto& dir : dirs) {
        fs::path path = dir / (name + ".pack");
        std::error_code ec;
        if (!fs::exists(path, ec)) {
            tried += " " + path.string();
            continue;
        }
        auto file = MappedFile::open(path);
        if (!file) return std::unexpected(file.error());
        auto pack = ShaderPack::parse(file->bytes());
        if (!pack) return std::unexpected(pack.error());
        return ShaderLibrary{std::move(*file), std::move(*pack)};
    }
    return fail(Errc::Io, "shader library " + name + ".pack not found; tried:" + tried);
}

} // namespace obgi
