#include "text/font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <cstdlib>
#include <fstream>

#include "text/encoding.hpp"

namespace obgi {

namespace fs = std::filesystem;

namespace {

using FileData = std::shared_ptr<std::vector<std::uint8_t>>;

std::string to_utf8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) out.push_back(char(c));
        else if (c < 0x800) out.push_back(char(0xC0 | c >> 6)), out.push_back(char(0x80 | (c & 0x3F)));
        else if (c < 0x10000)
            out.push_back(char(0xE0 | c >> 12)), out.push_back(char(0x80 | ((c >> 6) & 0x3F))), out.push_back(char(0x80 | (c & 0x3F)));
        else
            out.push_back(char(0xF0 | c >> 18)), out.push_back(char(0x80 | ((c >> 12) & 0x3F))),
                out.push_back(char(0x80 | ((c >> 6) & 0x3F))), out.push_back(char(0x80 | (c & 0x3F)));
    }
    return out;
}

std::string lower(std::string s) {
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return s;
}

// Windows font names used by the scripts -> candidate file names (lower case), best first.
std::vector<std::string> candidates_for(const std::string& name_utf8) {
    std::string n = lower(name_utf8);
    // full-width spaces/letters in "ＭＳ ゴシック" arrive as UTF-8 multi-byte sequences; compare on the decoded text
    std::u32string u = utf8_to_utf32(name_utf8);
    for (char32_t& c : u) {
        if (c >= 0xFF21 && c <= 0xFF3A) c = c - 0xFF21 + 'a';
        else if (c >= 0xFF41 && c <= 0xFF5A) c = c - 0xFF41 + 'a';
        else if (c == 0x3000) c = ' ';
        else if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    }
    std::string k = to_utf8(u);
    std::vector<std::string> out;
    if (k.find("ms gothic") != std::string::npos || k.find("ms ゴシック") != std::string::npos || k.find("ms ｺﾞｼｯｸ") != std::string::npos)
        out = {"msgothic.ttc", "msgothic.ttf"};
    else if (k.find("ms mincho") != std::string::npos || k.find("ms 明朝") != std::string::npos)
        out = {"msmincho.ttc", "msmincho.ttf"};
    else if (k.find("meiryo") != std::string::npos || k.find("メイリオ") != std::string::npos)
        out = {"meiryo.ttc"};
    else if (k.find("yu gothic") != std::string::npos || k.find("游ゴシック") != std::string::npos)
        out = {"yugothm.ttc", "yugothr.ttc"};
    out.push_back(n + ".ttf");
    out.push_back(n + ".ttc");
    out.push_back(n + ".otf");
    return out;
}

// Installed fonts that cover Japanese, tried when the named font is not available.
const char* const kFallbacks[] = {"msgothic.ttc", "yugothr.ttc", "yugothm.ttc", "meiryo.ttc", "msmincho.ttc", "msyh.ttc",
                                  "NotoSansCJK-Regular.ttc", "NotoSansCJKjp-Regular.otf", "NotoSansJP-Regular.otf",
                                  "HiraginoSans-W3.ttc", "ヒラギノ角ゴシック W3.ttc", "DroidSansFallbackFull.ttf",
                                  "DroidSansFallback.ttf", "simsun.ttc"};

} // namespace

struct Font::Impl {
    FT_Face face = nullptr;
    FileData data;
    std::map<char32_t, std::unique_ptr<Glyph>> cache; // null entry = missing glyph
    ~Impl() {
        if (face) FT_Done_Face(face);
    }
};

Font::~Font() = default;

const Glyph* Font::glyph(char32_t cp) {
    auto it = impl_->cache.find(cp);
    if (it != impl_->cache.end()) return it->second.get();
    std::unique_ptr<Glyph> g;
    FT_Face face = impl_->face;
    if (FT_Get_Char_Index(face, cp) != 0 && FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL) == 0) {
        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap& bm = slot->bitmap;
        g = std::make_unique<Glyph>();
        g->left = slot->bitmap_left;
        g->top = slot->bitmap_top;
        g->width = static_cast<int>(bm.width);
        g->height = static_cast<int>(bm.rows);
        g->advance = static_cast<int>((slot->advance.x + 32) >> 6);
        g->coverage.resize(std::size_t(g->width) * g->height);
        for (int y = 0; y < g->height; ++y) {
            const std::uint8_t* row = bm.buffer + std::ptrdiff_t(y) * bm.pitch;
            for (int x = 0; x < g->width; ++x) g->coverage[std::size_t(y) * g->width + x] = bm.pixel_mode == FT_PIXEL_MODE_MONO ? ((row[x >> 3] >> (7 - (x & 7))) & 1) * 255 : row[x];
        }
    }
    return impl_->cache.emplace(cp, std::move(g)).first->second.get();
}

struct FontLibrary::Impl {
    FT_Library ft = nullptr;
    std::vector<fs::path> dirs;
    std::map<fs::path, FileData> files;
    std::map<std::pair<fs::path, int>, std::weak_ptr<Font>> fonts;
    ~Impl() {
        fonts.clear();
        if (ft) FT_Done_FreeType(ft);
    }
};

FontLibrary::~FontLibrary() = default;

Result<std::unique_ptr<FontLibrary>> FontLibrary::create() {
    std::unique_ptr<FontLibrary> lib(new FontLibrary);
    lib->impl_ = std::make_unique<Impl>();
    if (FT_Init_FreeType(&lib->impl_->ft) != 0) return fail(Errc::Io, "FreeType initialisation failed");
    if (const char* env = std::getenv("OBGI_FONT_DIR")) lib->impl_->dirs.emplace_back(env);
    return lib;
}

void FontLibrary::add_search_dir(fs::path dir) { impl_->dirs.push_back(std::move(dir)); }

Result<fs::path> FontLibrary::resolve(const std::string& cp932_name) const { return resolve_impl(cp932_name, true); }

bool FontLibrary::has_exact(const std::string& cp932_name) const { return resolve_impl(cp932_name, false).has_value(); }

Result<fs::path> FontLibrary::resolve_impl(const std::string& cp932_name, bool fallback) const {
    std::vector<fs::path> dirs = impl_->dirs;
    for (const char* d : {"C:/Windows/Fonts", "/usr/share/fonts", "/usr/share/fonts/truetype", "/usr/share/fonts/opentype",
                          "/usr/share/fonts/opentype/noto", "/usr/share/fonts/truetype/noto", "/usr/local/share/fonts",
                          "/System/Library/Fonts", "/Library/Fonts"})
        dirs.emplace_back(d);
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(fs::path(home) / ".fonts");
        dirs.push_back(fs::path(home) / "Library/Fonts");
    }

    auto find = [&](const std::string& file) -> fs::path {
        const std::string want = lower(file);
        for (const fs::path& dir : dirs) {
            std::error_code ec;
            if (!fs::is_directory(dir, ec)) continue;
            // direct hit first (the common case), then a shallow recursive scan for distro layouts
            if (fs::path p = dir / file; fs::exists(p, ec)) return p;
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
                 it.increment(ec)) {
                if (it.depth() > 3) it.disable_recursion_pending();
                if (it->is_regular_file(ec) && lower(it->path().filename().string()) == want) return it->path();
            }
        }
        return {};
    };

    const std::string utf8 = to_utf8(cp932_to_utf32(cp932_name));
    for (const std::string& file : candidates_for(utf8))
        if (fs::path p = find(file); !p.empty()) return p;
    if (!fallback) return fail(Errc::Io, "font not installed: " + utf8);
    for (const char* file : kFallbacks)
        if (fs::path p = find(file); !p.empty()) return p;
    return fail(Errc::Io, "no usable font found for \"" + utf8 + "\"; set OBGI_FONT_DIR to a folder with a Japanese font");
}

Result<std::shared_ptr<Font>> FontLibrary::open(const std::string& cp932_name, int pixel_size) {
    Impl& m = *impl_;
    if (pixel_size < 4 || pixel_size > 512) return fail(Errc::Range, "font size out of range");
    auto path = resolve(cp932_name);
    if (!path) return std::unexpected(path.error());
    const auto key = std::make_pair(*path, pixel_size);
    if (auto it = m.fonts.find(key); it != m.fonts.end())
        if (auto sp = it->second.lock()) return sp;

    FileData data;
    if (auto it = m.files.find(*path); it != m.files.end()) {
        data = it->second;
    } else {
        std::ifstream f(*path, std::ios::binary);
        if (!f) return fail(Errc::Io, "cannot read font " + path->string());
        data = std::make_shared<std::vector<std::uint8_t>>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        m.files.emplace(*path, data);
    }

    std::shared_ptr<Font> font(new Font);
    font->impl_ = std::make_unique<Font::Impl>();
    font->impl_->data = data;
    if (FT_New_Memory_Face(m.ft, data->data(), static_cast<FT_Long>(data->size()), 0, &font->impl_->face) != 0)
        return fail(Errc::Format, "FreeType cannot open " + path->string());
    FT_Face face = font->impl_->face;
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0) return fail(Errc::Format, "font has no Unicode map: " + path->string());
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_size)) != 0) return fail(Errc::Format, "font size not available");
    const FT_Size_Metrics& sm = face->size->metrics;
    font->ascent_ = static_cast<int>((sm.ascender + 63) >> 6);
    font->descent_ = static_cast<int>((-sm.descender + 63) >> 6);
    font->line_height_ = std::max(static_cast<int>((sm.height + 63) >> 6), font->ascent_ + font->descent_);
    m.fonts[key] = font;
    return font;
}

} // namespace obgi
