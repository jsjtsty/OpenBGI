#include "vfs/arc.hpp"

#include <algorithm>
#include <cstring>

namespace obgi {

namespace {
constexpr std::size_t kHeaderSize = 16;
constexpr std::size_t kEntrySize = 0x80;
constexpr std::size_t kNameSize = 0x60;
constexpr char kMagic[] = "BURIKO ARC20";
constexpr char kPackMagic[] = "PackFile    ";
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
    if (b.size() < kHeaderSize) return fail(Errc::Format, "truncated archive header: " + path.string());
    const bool pack = std::memcmp(b.data(), kPackMagic, 12) == 0;
    if (!pack && std::memcmp(b.data(), kMagic, 12) != 0)
        return fail(Errc::Format, "unsupported archive format: " + path.string());
    const std::size_t entry_size = pack ? 0x20 : kEntrySize;
    const std::size_t name_size = pack ? 0x10 : kNameSize;
    std::uint32_t count = load_u32le(b.data() + 12);
    if (count > (b.size() - kHeaderSize) / entry_size)
        return fail(Errc::Format, "truncated archive directory: " + path.string());
    std::size_t base = kHeaderSize + static_cast<std::size_t>(count) * entry_size;

    Arc arc;
    arc.base_ = base;
    arc.entries_.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::uint8_t* e = b.data() + kHeaderSize + static_cast<std::size_t>(i) * entry_size;
        const char* name = reinterpret_cast<const char*>(e);
        ArcEntry entry;
        entry.name.assign(name, strnlen(name, name_size));
        entry.offset = load_u32le(e + name_size);
        entry.size = load_u32le(e + name_size + 4);
        if (entry.offset > b.size() - base || entry.size > b.size() - base - entry.offset)
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
