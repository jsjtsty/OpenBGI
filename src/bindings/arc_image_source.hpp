#pragma once
#include <cstring>
#include <filesystem>
#include <map>
#include <list>
#include <string>
#include <string_view>
#include <vector>

#include "render/display.hpp"
#include "resources/decoded_cache.hpp"

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
    using Cache = DecodedCache<SlotImage>;
    using Request = Cache::Request;
    explicit ArcImageSource(ArcResourceFinder& finder, std::size_t cache_limit = 128u * 1024u * 1024u);
    void set_queue(DecodeQueue* queue) { cache_.set_queue(queue); }
    std::size_t cached_bytes() const { return cache_.bytes(); }
    std::size_t cached_images() const { return cache_.count(); }
    void clear_cache() { cache_.clear(); }
    Result<Request> request(std::string_view prefix, std::string_view name) { return cache_.request(prefix, name); }
    std::optional<Result<std::shared_ptr<const SlotImage>>> poll(const Request& request) { return cache_.poll(request); }
    Result<std::shared_ptr<const SlotImage>> load_shared(std::string_view prefix, std::string_view name) { return cache_.load_sync(prefix, name); }
    Result<SlotImage> load(std::string_view prefix, std::string_view name);
  private:
    static Result<SlotImage> decode(OwnedBytes raw);
    Cache cache_;

};

} // namespace obgi
