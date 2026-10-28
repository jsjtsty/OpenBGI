#include "bindings/arc_program_provider.hpp"

#include "formats/dsc.hpp"

namespace obgi {

ArcProgramProvider::ArcProgramProvider(std::filesystem::path game_dir) : dir_(std::move(game_dir)) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec))
        files_.emplace(ascii_lower(e.path().filename().string()), e.path());
}

Result<Arc*> ArcProgramProvider::open(std::string_view arc) {
    std::string key = ascii_lower(arc);
    if (auto it = arcs_.find(key); it != arcs_.end()) return &it->second;
    auto f = files_.find(key);
    if (f == files_.end()) return fail(Errc::Io, "archive not found: " + std::string(arc));
    auto opened = Arc::open(f->second);
    if (!opened) return std::unexpected(opened.error());
    return &arcs_.emplace(key, std::move(*opened)).first->second;
}

Result<std::vector<std::uint8_t>> ArcProgramProvider::load(std::string_view arc, std::string_view name) {
    auto a = open(arc);
    if (!a) return std::unexpected(a.error());
    const ArcEntry* e = (*a)->find(name);
    if (!e) return fail(Errc::Io, "program not found: " + std::string(arc) + "/" + std::string(name));
    ByteView data = (*a)->data(*e);
    if (is_dsc(data)) return dsc_decode(data);
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

} // namespace obgi
