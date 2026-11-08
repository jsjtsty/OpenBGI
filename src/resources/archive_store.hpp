#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "vfs/arc.hpp"

namespace obgi {

// Finds a resource in the game's archives by (archive prefix, member name) and returns its bytes,
// with the DSC layer removed when there is one.
class ArcResourceFinder {
  public:
    explicit ArcResourceFinder(std::filesystem::path game_dir);

    // `prefix` names the archive (with or without ".arc"); when it matches none, every archive is searched.
    Result<std::vector<std::uint8_t>> read(std::string_view prefix, std::string_view name, bool* from_loose = nullptr);

    Result<std::vector<std::uint8_t>> read_archive(std::string_view archive, std::string_view name);
    void add_search_path(std::filesystem::path path);

    // Whether read() would find it (no decoding).
    bool exists(std::string_view prefix, std::string_view name);
    // Loose resources always override archive entries and may change at runtime.
    bool has_loose_file(std::string_view name) const;
    std::optional<std::filesystem::path> find_loose_file(std::string_view name) const;
    // Check only archive members when the caller has already checked all loose-file locations.
    bool exists_in_archives(std::string_view prefix, std::string_view name);

  private:
    std::optional<ByteView> locate(std::string_view prefix, std::string_view name);
    Result<Arc*> open(const std::string& lower_file_name);

    std::optional<std::filesystem::path> loose_file(std::string_view name) const;
    std::vector<std::filesystem::path> search_paths_;
    std::filesystem::path dir_;
    std::map<std::string, std::filesystem::path> files_; // lowercase file name -> path
    std::map<std::string, Arc> arcs_;
    std::multimap<std::string, std::string> index_; // lowercase member -> archive file name
    std::set<std::string> scanned_; // archives whose member tables are already in index_
    bool indexed_ = false;
};

} // namespace obgi
