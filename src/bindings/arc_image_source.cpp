#include "bindings/arc_image_source.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

#include "formats/cbg.hpp"

namespace obgi {

ArcImageSource::ArcImageSource(ArcResourceFinder& finder, std::size_t cache_limit)
    : finder_(finder), cache_limit_(cache_limit) {}

void ArcImageSource::clear_cache() {
    cache_.clear(); lru_.clear(); cached_bytes_ = 0;
}

Result<SlotImage> ArcImageSource::load(std::string_view prefix, std::string_view name) {
    auto image = load_shared(prefix, name);
    if (!image) return std::unexpected(image.error());
    return **image; // compatibility for callers that need mutable pixels
}

Result<std::shared_ptr<const SlotImage>> ArcImageSource::load_shared(std::string_view prefix, std::string_view name) {
    // Loose files can change at runtime; always decode the latest override.
    const bool loose = finder_.has_loose_file(name);
    Key key{ascii_lower(prefix), ascii_lower(name)};
    if (!loose) if (auto it = cache_.find(key); it != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        return it->second.image;
    }
    bool from_loose = false;
    auto decoded = load_uncached(prefix, name, &from_loose);
    if (!decoded) return std::unexpected(decoded.error());
    std::shared_ptr<const SlotImage> image = std::make_shared<SlotImage>(std::move(*decoded));
    const std::size_t bytes = image->pixels.size() * sizeof(std::uint32_t);
    if (!loose && !from_loose && bytes && bytes <= cache_limit_) {
        while (cached_bytes_ > cache_limit_ - bytes) {
            auto victim = cache_.find(lru_.back());
            cached_bytes_ -= victim->second.image->pixels.size() * sizeof(std::uint32_t);
            cache_.erase(victim); lru_.pop_back();
        }
        lru_.push_front(key);
        cache_.emplace(std::move(key), CachedImage{image, lru_.begin()});
        cached_bytes_ += bytes;
    }
    return image;
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

Result<SlotImage> ArcImageSource::load_uncached(std::string_view prefix, std::string_view name, bool* from_loose) {
    auto bytes = finder_.open_media(prefix, name, from_loose);
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
