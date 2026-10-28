#pragma once
#include <cstring>
#include <filesystem>
#include <map>
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
};

// Loads pictures (CBG, optionally DSC-wrapped) from the game's archives by (archive prefix, member name).
class ArcImageSource {
  public:
    explicit ArcImageSource(ArcResourceFinder& finder);

    Result<SlotImage> load(std::string_view prefix, std::string_view name);

  private:
    ArcResourceFinder& finder_;
};

} // namespace obgi
