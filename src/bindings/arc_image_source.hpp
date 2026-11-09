#pragma once
#include <cstring>
#include <filesystem>
#include <map>
#include <list>
#include <string>
#include <string_view>
#include <vector>

#include "render/display.hpp"
#include "bindings/arc_resource_finder.hpp"

namespace obgi {

// A decoded picture ready to become an image slot: one packed uint32 per pixel (see render/display.hpp).
struct SlotImage {
    std::uint32_t width = 0, height = 0;
    SlotFormat format = SlotFormat::Bgrx;
    std::vector<std::uint32_t> pixels;
    bool has_base = false; // the file's base size (CBG header), see CbgImage
    std::uint32_t base_width = 0, base_height = 0;
};

// Loads pictures (CBG, optionally DSC-wrapped) from the game's archives by (archive prefix, member name).
class ArcImageSource {
  public:
    explicit ArcImageSource(ArcResourceFinder& finder, std::size_t cache_limit = 128u * 1024u * 1024u);
    std::size_t cached_bytes() const { return cached_bytes_; }
    std::size_t cached_images() const { return cache_.size(); }
    void clear_cache();

    // Shared immutable pixels avoid a full image copy on a cache hit.
    Result<std::shared_ptr<const SlotImage>> load_shared(std::string_view prefix, std::string_view name);
    Result<SlotImage> load(std::string_view prefix, std::string_view name);

  private:
    using Key = std::pair<std::string, std::string>;
    struct CachedImage { std::shared_ptr<const SlotImage> image; std::list<Key>::iterator lru; };
    Result<SlotImage> load_uncached(std::string_view prefix, std::string_view name, bool* from_loose = nullptr);
    ArcResourceFinder& finder_;
    std::size_t cache_limit_, cached_bytes_ = 0;
    std::list<Key> lru_;
    std::map<Key, CachedImage> cache_;
};

} // namespace obgi
