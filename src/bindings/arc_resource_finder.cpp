#include "bindings/arc_resource_finder.hpp"

#include "formats/dsc.hpp"

namespace obgi {

ArcResourceFinder::ArcResourceFinder(std::filesystem::path game_dir) : dir_(std::move(game_dir)) {
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec)) {
        std::string name = ascii_lower(e.path().filename().string());
        if (name.ends_with(".arc")) files_.emplace(std::move(name), e.path());
    }
}

Result<Arc*> ArcResourceFinder::open(const std::string& key) {
    if (auto it = arcs_.find(key); it != arcs_.end()) return &it->second;
    auto f = files_.find(key);
    if (f == files_.end()) return fail(Errc::Io, "archive not found: " + key);
    auto opened = Arc::open(f->second);
    if (!opened) return std::unexpected(opened.error());
    return &arcs_.emplace(key, std::move(*opened)).first->second;
}

void ArcResourceFinder::build_index() {
    indexed_ = true;
    for (const auto& [key, path] : files_) {
        auto a = open(key);
        if (!a) continue;
        for (const ArcEntry& e : (*a)->entries()) index_.emplace(ascii_lower(e.name), key);
    }
}

std::optional<ByteView> ArcResourceFinder::locate(std::string_view prefix, std::string_view name) {
    ByteView data;
    auto try_arc = [&](const std::string& key) -> bool {
        if (!files_.count(key)) return false;
        auto a = open(key);
        if (!a) return false;
        const ArcEntry* e = (*a)->find(name);
        if (!e) return false;
        data = (*a)->data(*e);
        return true;
    };

    std::string p = ascii_lower(prefix);
    bool found = try_arc(p) || try_arc(p + ".arc");
    if (!found) {
        if (!indexed_) build_index();
        auto range = index_.equal_range(ascii_lower(name));
        for (auto it = range.first; it != range.second && !found; ++it) found = try_arc(it->second);
    }
    if (!found) return std::nullopt;
    return data;
}

bool ArcResourceFinder::exists(std::string_view prefix, std::string_view name) { return locate(prefix, name).has_value(); }

Result<std::vector<std::uint8_t>> ArcResourceFinder::read(std::string_view prefix, std::string_view name) {
    auto found = locate(prefix, name);
    if (!found) return fail(Errc::Io, "image not found: " + std::string(prefix) + "/" + std::string(name));
    ByteView data = *found;
    if (is_dsc(data)) return dsc_decode(data);
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

} // namespace obgi
