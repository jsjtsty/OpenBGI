#include "bindings/arc_image_source.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

#include "formats/cbg.hpp"

namespace obgi {

ArcImageSource::ArcImageSource(ArcResourceFinder& finder, std::size_t limit)
    : cache_(finder, [](OwnedBytes raw) -> Result<std::shared_ptr<const SlotImage>> {
        auto image = decode(std::move(raw));
        if (!image) return std::unexpected(image.error());
        return std::make_shared<const SlotImage>(std::move(*image));
    }, [](const SlotImage& image) { return image.pixels.size() * sizeof(std::uint32_t); }, limit) {}

Result<SlotImage> ArcImageSource::load(std::string_view prefix, std::string_view name) {
    auto image = load_shared(prefix, name);
    if (!image) return std::unexpected(image.error());
    return **image;
}

namespace {

// The engine's plain image format (system graphics such as sysgrp "SGT..." members, usually inside a DSC layer):
// u16 width, u16 height, u32 bits per pixel (8, 24 or 32), 8 zero bytes, then the rows top to bottom with no padding
// (B, G, R[, A]). Recognised only when the size matches exactly.
std::optional<SlotImage> decode_plain_image(ByteView d) {
    if (d.size() < 16) return std::nullopt;
    const std::uint32_t w = d[0] | d[1] << 8, h = d[2] | d[3] << 8, bpp = load_u32le(d.data() + 4);
    if (w == 0 || h == 0 || (bpp != 8 && bpp != 24 && bpp != 32)) return std::nullopt;
    if (load_u32le(d.data() + 8) != 0 || load_u32le(d.data() + 12) != 0) return std::nullopt;
    const std::size_t n = std::size_t{w} * h;
    if (d.size() != 16 + n * (bpp / 8)) return std::nullopt;
    SlotImage out;
    out.width = w;
    out.height = h;
    out.pixels.resize(n);
    const std::uint8_t* p = d.data() + 16;
    switch (bpp) {
    case 8:
        out.format = SlotFormat::Gray8;
        for (std::size_t i = 0; i < n; ++i) out.pixels[i] = p[i];
        break;
    case 24:
        out.format = SlotFormat::Bgrx;
        for (std::size_t i = 0; i < n; ++i) out.pixels[i] = p[3 * i] | p[3 * i + 1] << 8 | p[3 * i + 2] << 16;
        break;
    default:
        out.format = SlotFormat::Bgra;
        std::memcpy(out.pixels.data(), p, n * 4);
        break;
    }
    return out;
}

} // namespace

Result<SlotImage> ArcImageSource::decode(OwnedBytes raw) {
    auto bytes = unpack_resource(std::move(raw));
    if (!bytes) return std::unexpected(bytes.error());
    ByteView data = bytes->bytes;
    if (!is_cbg(data))
        if (auto plain = decode_plain_image(data)) return std::move(*plain);
    auto img = cbg_decode(data);
    if (!img) {
        std::string head;
        char hex[4];
        for (std::size_t i = 0; i < std::min<std::size_t>(24, data.size()); ++i) {
            std::snprintf(hex, sizeof hex, "%02x ", data[i]);
            head += hex;
        }
        return fail(img.error().code, img.error().message + " (" + std::to_string(data.size()) + " bytes: " + head + ")");
    }

    SlotImage out;
    out.width = img->width;
    out.height = img->height;
    out.has_base = img->has_base, out.base_width = img->base_width, out.base_height = img->base_height;
    const std::size_t n = std::size_t{img->width} * img->height;
    out.pixels.resize(n);
    const std::uint8_t* p8 = img->pixels.data();
    switch (img->bits_per_pixel) {
    case 8:
        out.format = SlotFormat::Gray8;
        for (std::size_t i = 0; i < n; ++i) out.pixels[i] = p8[i];
        break;
    case 24:
        out.format = SlotFormat::Bgrx;
        for (std::size_t i = 0; i < n; ++i) out.pixels[i] = p8[3 * i] | p8[3 * i + 1] << 8 | p8[3 * i + 2] << 16;
        break;
    default: // 32: B, G, R, A is exactly the packed little-endian layout
        out.format = SlotFormat::Bgra;
        std::memcpy(out.pixels.data(), p8, n * 4);
        break;
    }
    return out;
}

} // namespace obgi
