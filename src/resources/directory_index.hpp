#pragma once

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "vfs/arc.hpp"

namespace obgi {

// A negative-lookup index valid for one VM frame. Positive candidates are
// checked on disk, preserving regular-file and symlink semantics. Call clear()
// after the engine changes the filesystem; external changes appear next frame.
class DirectoryIndex {
  public:
    bool is_regular_file(const std::filesystem::path& path, std::uint64_t frame) {
        if (frame_ != frame) { entries_.clear(); frame_ = frame; }
        const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
        const auto name = path.filename().string();
        if (name.empty() || name == "." || name == "..") return regular(path);
        const auto key = parent.lexically_normal().string();
        if (entries_.size() >= 64 && !entries_.contains(key)) return regular(path);
        auto [it, inserted] = entries_.try_emplace(key);
        if (inserted) {
            std::error_code ec;
            std::filesystem::directory_iterator current(parent, ec), end;
            if (!ec) {
                for (; current != end; current.increment(ec)) {
                    if (ec || it->second.names.size() >= 16384) break;
                    auto entry = current->path().filename().string();
                    if (entry.find('\\') != std::string::npos) it->second.has_backslash = true;
                    if (std::any_of(entry.begin(), entry.end(), [](unsigned char c) { return c >= 0x80; })) it->second.has_non_ascii = true;
                    it->second.names.insert(ascii_lower(entry));
                }
                it->second.valid = !ec && current == end;
            }
        }
        // Case-insensitive filesystems may fold non-ASCII characters in ways
        // ASCII folding cannot model; defer those names to the filesystem.
        const bool non_ascii = std::any_of(name.begin(), name.end(), [](unsigned char c) { return c >= 0x80; });
        if (it->second.valid && name.find('\\') != std::string::npos && !it->second.has_backslash) return false;
        if (it->second.valid && !non_ascii && !it->second.has_non_ascii && !it->second.names.contains(ascii_lower(name))) return false;
        return regular(path);
    }
    void clear() { entries_.clear(); }

  private:
    struct Entries { bool valid = false, has_backslash = false, has_non_ascii = false; std::unordered_set<std::string> names; };
    static bool regular(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::is_regular_file(path, ec);
    }
    std::uint64_t frame_ = std::numeric_limits<std::uint64_t>::max();
    std::unordered_map<std::string, Entries> entries_;
};

} // namespace obgi
