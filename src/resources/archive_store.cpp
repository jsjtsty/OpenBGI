#include "resources/archive_store.hpp"
#include <fstream>

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
    if (auto it = arcs_.find(key); it != arcs_.end()) return it->second.get();
    auto f = files_.find(key);
    if (f == files_.end()) return fail(Errc::Io, "archive not found: " + key);
    auto opened = Arc::open(f->second);
    if (!opened) return std::unexpected(opened.error());
    return arcs_.emplace(key, std::make_shared<Arc>(std::move(*opened))).first->second.get();
}

std::optional<MediaResource> ArcResourceFinder::locate_owned(std::string_view prefix, std::string_view name) {
    MediaResource data;
    auto try_arc = [&](const std::string& key) -> bool {
        if (!files_.count(key)) return false;
        auto a = open(key);
        if (!a) return false;
        const ArcEntry* e = (*a)->find(name);
        if (!e) return false;
        data = {arcs_.at(key), (*a)->data(*e)};
        return true;
    };

    std::string p = ascii_lower(prefix);
    bool found = try_arc(p) || try_arc(p + ".arc");
    if (!found) {
        const std::string wanted = ascii_lower(name);
        auto try_index = [&]() {
            auto range = index_.equal_range(wanted);
            for (auto it = range.first; it != range.second && !found; ++it) found = try_arc(it->second);
        };
        try_index();
        // A missing archive prefix used to force a full scan of every archive
        // before returning the first match. Index progressively and stop once
        // the requested member is found, so common fallback lookups stay cheap.
        for (const auto& [key, path] : files_) {
            if (found || indexed_) break;
            if (!scanned_.insert(key).second) continue;
            auto a = open(key);
            if (a)
                for (const ArcEntry& e : (*a)->entries()) index_.emplace(ascii_lower(e.name), key);
            try_index();
        }
        indexed_ = scanned_.size() == files_.size();
    }
    if (!found) return std::nullopt;
    return data;
}

std::optional<ByteView> ArcResourceFinder::locate(std::string_view prefix, std::string_view name) {
    auto resource = locate_owned(prefix, name);
    return resource ? std::optional<ByteView>(resource->bytes) : std::nullopt;
}

Result<MediaResource> ArcResourceFinder::open_media(std::string_view prefix, std::string_view name, bool* from_loose) {
    if (from_loose) *from_loose = false;
    if (const auto path = loose_file(name)) {
        if (from_loose) *from_loose = true;
        auto mapped = MappedFile::open(*path);
        if (!mapped) return std::unexpected(mapped.error());
        auto owner = std::make_shared<MappedFile>(std::move(*mapped));
        MediaResource result{owner, owner->bytes()};
        if (!is_dsc(result.bytes)) return result;
        auto decoded = dsc_decode(result.bytes);
        if (!decoded) return std::unexpected(decoded.error());
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(*decoded));
        return MediaResource{bytes, ByteView(*bytes)};
    }
    auto key = ascii_lower(prefix);
    if (!key.ends_with(".arc")) key += ".arc";
    if (files_.contains(key)) {
        auto opened = open(key);
        if (!opened) return std::unexpected(opened.error());
    }
    auto result = locate_owned(prefix, name);
    if (!result) return fail(Errc::Io, "resource not found: " + std::string(prefix) + "/" + std::string(name));
    if (!is_dsc(result->bytes)) return *result;
    auto decoded = dsc_decode(result->bytes);
    if (!decoded) return std::unexpected(decoded.error());
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(std::move(*decoded));
    return MediaResource{bytes, ByteView(*bytes)};
}

void ArcResourceFinder::add_search_path(std::filesystem::path path) {
    if (path.is_relative()) path = dir_ / path;
    search_paths_.insert(search_paths_.begin(), std::move(path));
}

std::optional<std::filesystem::path> ArcResourceFinder::loose_file(std::string_view name) const {
    std::string normalized(name);
    for (auto& ch : normalized) if (ch == '\\') ch = '/';
    const std::filesystem::path relative(std::u8string(normalized.begin(), normalized.end()));
    std::error_code ec;
    for (const auto& root : search_paths_) {
        auto path = root / relative;
        if (std::filesystem::is_regular_file(path, ec)) return path;
    }
    auto path = dir_ / relative;
    if (std::filesystem::is_regular_file(path, ec)) return path;
    return std::nullopt;
}

std::optional<std::filesystem::path> ArcResourceFinder::find_loose_file(std::string_view name) const {
    return loose_file(name);
}

bool ArcResourceFinder::has_loose_file(std::string_view name) const {
    return loose_file(name).has_value();
}

bool ArcResourceFinder::exists(std::string_view prefix, std::string_view name) {
    return loose_file(name).has_value() || locate(prefix, name).has_value();
}

bool ArcResourceFinder::exists_in_archives(std::string_view prefix, std::string_view name) {
    return locate(prefix, name).has_value();
}

Result<std::vector<std::uint8_t>> ArcResourceFinder::read_archive(std::string_view archive, std::string_view name) {
    auto key = ascii_lower(archive);
    if (!key.ends_with(".arc")) key += ".arc";
    auto opened = open(key);
    if (!opened) return std::unexpected(opened.error());
    const auto entry = (*opened)->find(name);
    if (!entry) return fail(Errc::Io, "resource not found: " + key + "/" + std::string(name));
    const auto bytes = (*opened)->data(*entry);
    if (is_dsc(bytes)) return dsc_decode(bytes);
    return std::vector<std::uint8_t>(bytes.begin(), bytes.end());
}

Result<std::vector<std::uint8_t>> ArcResourceFinder::read(std::string_view prefix, std::string_view name, bool* from_loose) {
    if (from_loose) *from_loose = false;
    if (const auto path = loose_file(name)) {
        if (from_loose) *from_loose = true;
        std::ifstream input(*path, std::ios::binary | std::ios::ate);
        if (!input) return fail(Errc::Io, "cannot open resource: " + path->string());
        const auto size = input.tellg();
        if (size < 0) return fail(Errc::Io, "cannot size resource: " + path->string());
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.seekg(0);
        if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return fail(Errc::Io, "cannot read resource: " + path->string());
        if (is_dsc(bytes)) return dsc_decode(bytes);
        return bytes;
    }
    // A corrupt explicitly selected archive must not be disguised as a missing member.
    auto key = ascii_lower(prefix);
    if (!key.ends_with(".arc")) key += ".arc";
    if (files_.contains(key)) {
        auto opened = open(key);
        if (!opened) return std::unexpected(opened.error());
    }
    auto found = locate(prefix, name);
    if (!found) return fail(Errc::Io, "resource not found: " + std::string(prefix) + "/" + std::string(name));
    ByteView data = *found;
    if (is_dsc(data)) return dsc_decode(data);
    return std::vector<std::uint8_t>(data.begin(), data.end());
}

} // namespace obgi
