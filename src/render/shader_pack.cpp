#include "render/shader_pack.hpp"

#include <cstring>

namespace obgi {

using namespace shader_pack;

namespace {
// Entry layout: name[32] @0, entry point[16] @32, format @48, offset @52, size @56, pad @60.
constexpr std::size_t kEntryPointOffset = 32, kFormatOffset = 48, kBlobOffset = 52, kBlobSize = 56;

std::string_view cstr(const std::uint8_t* p, std::size_t max) {
    return {reinterpret_cast<const char*>(p), strnlen(reinterpret_cast<const char*>(p), max)};
}
} // namespace

Result<ShaderPack> ShaderPack::parse(ByteView pack) {
    if (pack.size() < kHeaderSize || std::memcmp(pack.data(), kMagic, 4) != 0)
        return fail(Errc::Format, "not a shader pack");
    if (load_u32le(pack.data() + 4) != kVersion) return fail(Errc::Unsupported, "shader pack version");
    ShaderPack p;
    p.data_ = pack;
    p.count_ = load_u32le(pack.data() + 8);
    if (static_cast<std::uint64_t>(p.count_) * kEntrySize > pack.size() - kHeaderSize)
        return fail(Errc::Format, "truncated shader pack directory");
    for (std::uint32_t i = 0; i < p.count_; ++i) {
        const std::uint8_t* e = pack.data() + kHeaderSize + i * kEntrySize;
        std::uint64_t off = load_u32le(e + kBlobOffset), size = load_u32le(e + kBlobSize);
        if (off + size > pack.size()) return fail(Errc::Format, "shader blob outside pack");
    }
    return p;
}

std::optional<ShaderBlob> ShaderPack::find(std::string_view name, std::uint32_t supported_formats) const {
    for (std::uint32_t i = 0; i < count_; ++i) {
        const std::uint8_t* e = data_.data() + kHeaderSize + i * kEntrySize;
        if (cstr(e, kNameSize) != name) continue;
        std::uint32_t format = load_u32le(e + kFormatOffset);
        if (!(format & supported_formats)) continue;
        return ShaderBlob{format, cstr(e + kEntryPointOffset, kEntryPointSize),
                          data_.subspan(load_u32le(e + kBlobOffset), load_u32le(e + kBlobSize))};
    }
    return std::nullopt;
}

} // namespace obgi
