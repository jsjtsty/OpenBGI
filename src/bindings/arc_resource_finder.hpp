#pragma once
#include <filesystem>
#include <map>
#include <optional>
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
    Result<std::vector<std::uint8_t>> read(std::string_view prefix, std::string_view name);

    // Whether read() would find it (no decoding).
    bool exists(std::string_view prefix, std::string_view name);

  private:
    std::optional<ByteView> locate(std::string_view prefix, std::string_view name);
    Result<Arc*> open(const std::string& lower_file_name);
    void build_index();

    std::filesystem::path dir_;
    std::map<std::string, std::filesystem::path> files_; // lowercase file name -> path
    std::map<std::string, Arc> arcs_;
    std::multimap<std::string, std::string> index_; // lowercase member -> archive file name
    bool indexed_ = false;
};

} // namespace obgi
