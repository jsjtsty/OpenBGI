#pragma once
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "vfs/mapped_file.hpp"

namespace obgi {

struct ArcEntry {
    std::string name; // raw CP932 bytes, as stored
    std::uint32_t offset = 0; // relative to the end of the directory
    std::uint32_t size = 0;
};

// "BURIKO ARC20" archive: 16-byte header (magic[12], count u32), count * 0x80 directory
// entries (name[0x60], offset u32, size u32, padding), then the member data.
class Arc {
  public:
    static Result<Arc> open(const std::filesystem::path& path);

    const std::vector<ArcEntry>& entries() const { return entries_; }
    // Case-insensitive (ASCII) lookup.
    const ArcEntry* find(std::string_view name) const;
    ByteView data(const ArcEntry& e) const { return file_.bytes().subspan(base_ + e.offset, e.size); }

  private:
    MappedFile file_;
    std::vector<ArcEntry> entries_;
    std::unordered_map<std::string, std::size_t> index_;
    std::size_t base_ = 0;
};

std::string ascii_lower(std::string_view s);

} // namespace obgi
