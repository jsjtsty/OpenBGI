#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/error.hpp"

namespace obgi {

// One rasterized glyph: 8-bit coverage plus placement relative to the pen on the baseline.
struct Glyph {
    int left = 0, top = 0;     // bitmap origin: x right of the pen, y above the baseline
    int width = 0, height = 0; // bitmap size in pixels
    int advance = 0;           // pen movement in pixels
    std::vector<std::uint8_t> coverage;
};

class FontLibrary;

// A font face at one pixel size, with a glyph cache.
class Font {
  public:
    ~Font();
    Font(const Font&) = delete;
    Font& operator=(const Font&) = delete;

    int ascent() const { return ascent_; }
    int descent() const { return descent_; }   // positive, below the baseline
    int line_height() const { return line_height_; }
    // Nullptr when the face has no glyph for the code point.
    const Glyph* glyph(char32_t cp);

  private:
    friend class FontLibrary;
    Font() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int ascent_ = 0, descent_ = 0, line_height_ = 0;
};

// Finds font files and opens faces (FreeType). Fonts are looked up by file: the game's script names a
// Windows font ("ＭＳ ゴシック"), which is mapped to a file name and searched for in, in order,
// $OBGI_FONT_DIR, the directories added with add_search_dir, then the system font folders.
class FontLibrary {
  public:
    static Result<std::unique_ptr<FontLibrary>> create();
    ~FontLibrary();

    void add_search_dir(std::filesystem::path dir);
    // The file a script font name resolves to, falling back to any installed CJK-capable font.
    Result<std::filesystem::path> resolve(const std::string& cp932_name) const;
    Result<std::shared_ptr<Font>> open(const std::string& cp932_name, int pixel_size);
    // Whether a font file exists for exactly this name (no fallback to another font).
    bool has_exact(const std::string& cp932_name) const;

  private:
    Result<std::filesystem::path> resolve_impl(const std::string& cp932_name, bool fallback) const;
    FontLibrary() = default;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace obgi
