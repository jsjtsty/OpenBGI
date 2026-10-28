#include "vfs/arc.hpp"

#include <algorithm>
#include <cstring>

namespace obgi {

namespace {
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kEntrySize = 0x80;
constexpr std::size_t kNameSize = 0x60;
constexpr char kMagic[] = "BURIKO ARC20";
} // namespace

std::string ascii_lower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

Result<Arc> Arc::open(const std::filesystem::path& path) {
    auto file = MappedFile::open(path);
    if (!file) return std::unexpected(file.error());
    ByteView b = file->bytes();
    if (b.size() < kHeaderSize || std::memcmp(b.data(), kMagic, 12) != 0)
        return fail(Errc::Format, "not a BURIKO ARC20 archive: " + path.string());
    std::uint32_t count = load_u32le(b.data() + 12);
    std::size_t base = kHeaderSize + static_cast<std::size_t>(count) * kEntrySize;
    if (base > b.size()) return fail(Errc::Format, "truncated ARC20 directory: " + path.string());

    Arc arc;
    arc.base_ = base;
    arc.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = b.data() + kHeaderSize + static_cast<std::size_t>(i) * kEntrySize;
        const char* name = reinterpret_cast<const char*>(e);
        ArcEntry entry;
        entry.name.assign(name, strnlen(name, kNameSize));
        entry.offset = load_u32le(e + kNameSize);
        entry.size = load_u32le(e + kNameSize + 4);
        if (base + entry.offset + static_cast<std::size_t>(entry.size) > b.size())
            return fail(Errc::Format, "member outside archive: " + entry.name);
        arc.entries_.push_back(std::move(entry));
    }
    for (std::size_t i = 0; i < arc.entries_.size(); ++i)
        arc.index_.emplace(ascii_lower(arc.entries_[i].name), i);
    arc.file_ = std::move(*file);
    return arc;
}

const ArcEntry* Arc::find(std::string_view name) const {
    auto it = index_.find(ascii_lower(name));
    return it == index_.end() ? nullptr : &entries_[it->second];
}

} // namespace obgi
