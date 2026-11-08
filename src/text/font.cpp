#include "text/font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <tuple>

#include "text/encoding.hpp"
#include "vfs/mapped_file.hpp"

namespace obgi {

namespace fs = std::filesystem;

namespace {

struct FontData {
    std::vector<std::uint8_t> owned;
    MappedFile mapped;
    const std::uint8_t* data() const { return owned.empty() ? mapped.bytes().data() : owned.data(); }
    std::size_t size() const { return owned.empty() ? mapped.bytes().size() : owned.size(); }
};
using FileData = std::shared_ptr<FontData>;

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
    out.push_back(n); // already a file name
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

// File names tried per glyph, after the named font, when it lacks a code point.
const char* const kGlyphFallbacks[] = {"msyh.ttc", "simsun.ttc", "msjh.ttc", "yugothr.ttc", "meiryo.ttc",
                                       "NotoSansCJK-Regular.ttc", "NotoSansSC-Regular.otf", "DroidSansFallbackFull.ttf"};

} // namespace

struct FreeTypeState {
    FT_Library ft = nullptr;
    ~FreeTypeState() { if (ft) FT_Done_FreeType(ft); }
};

struct Font::Impl {
    std::shared_ptr<FreeTypeState> engine;
    FT_Face face = nullptr;
    FileData data;
    std::map<char32_t, std::unique_ptr<Glyph>> cache; // null entry = missing glyph
    struct Fallback { fs::path path; std::shared_ptr<Font> font; bool attempted = false; };
    std::vector<Fallback> fallbacks;
    std::function<std::shared_ptr<Font>(const fs::path&)> open_fallback;
    bool fallbacks_ready = false;
    ~Impl() {
        if (face) FT_Done_Face(face);
    }
};

Font::~Font() = default;

const Glyph* Font::glyph(char32_t cp) {
    if (const auto glyph = native_glyph(cp)) return glyph;
    for (auto& fallback : impl_->fallbacks) {
        if (!fallback.attempted) {
            fallback.attempted = true;
            fallback.font = impl_->open_fallback(fallback.path);
        }
        if (fallback.font)
            if (const auto glyph = fallback.font->native_glyph(cp)) return glyph;
    }
    return nullptr;
}

const Glyph* Font::native_glyph(char32_t cp) {
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
    std::shared_ptr<FreeTypeState> engine = std::make_shared<FreeTypeState>();
    FT_Library ft = nullptr;
    std::map<std::string, fs::path> registered;
    std::map<fs::path, FT_Long> face_indices;
    mutable std::map<std::tuple<std::string, bool, int>, Result<fs::path>> resolutions;
    std::vector<fs::path> dirs;
    std::map<fs::path, FileData> files;
    struct CachedFont { std::shared_ptr<Font> font; std::uint64_t used; };
    std::map<std::pair<fs::path, int>, CachedFont> fonts;
    std::uint64_t font_clock = 0;
    ~Impl() {
        fonts.clear();
    }
};

// Cached primary fonts keep the state alive for lazy fallback after the library is gone.
// Clear the cache first to break the state -> font -> state ownership cycle.
FontLibrary::~FontLibrary() { if (impl_) impl_->fonts.clear(); }

Result<std::unique_ptr<FontLibrary>> FontLibrary::create() {
    std::unique_ptr<FontLibrary> lib(new FontLibrary);
    lib->impl_ = std::make_shared<Impl>();
    if (FT_Init_FreeType(&lib->impl_->engine->ft) != 0) return fail(Errc::Io, "FreeType initialisation failed");
    lib->impl_->ft = lib->impl_->engine->ft;
    if (const char* env = std::getenv("OBGI_FONT_DIR")) lib->impl_->dirs.emplace_back(env);
    return lib;
}

void FontLibrary::add_search_dir(fs::path dir) { impl_->dirs.push_back(std::move(dir)); impl_->resolutions.clear(); }

Result<void> FontLibrary::register_file(const std::string& name, std::vector<std::uint8_t> bytes) {
    auto data = std::make_shared<FontData>();
    data->owned = std::move(bytes);
    if (data->size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max())) return fail(Errc::Range, "font resource is too large: " + name);
    FT_Face first = nullptr;
    if (FT_New_Memory_Face(impl_->ft, data->data(), static_cast<FT_Long>(data->size()), 0, &first) != 0)
        return fail(Errc::Format, "invalid font resource: " + name);
    const auto count = first->num_faces;
    FT_Done_Face(first);
    for (FT_Long index = 0; index < count; ++index) {
        FT_Face face = nullptr;
        if (FT_New_Memory_Face(impl_->ft, data->data(), static_cast<FT_Long>(data->size()), index, &face) != 0) continue;
        const auto identity = fs::path("@memory") / (std::to_string(impl_->files.size()) + ".font");
        impl_->files.emplace(identity, data);
        impl_->face_indices.emplace(identity, index);
        if (index == 0) impl_->registered[lower(name)] = identity;
        if (face->family_name) impl_->registered.try_emplace(lower(face->family_name), identity);
        if (const auto postscript = FT_Get_Postscript_Name(face)) impl_->registered[lower(postscript)] = identity;
        FT_Done_Face(face);
    }
    impl_->resolutions.clear();
    return {};
}

Result<fs::path> FontLibrary::resolve(const std::string& cp932_name) const { return resolve_impl(cp932_name, true); }

bool FontLibrary::has_exact(const std::string& cp932_name) const { return resolve_impl(cp932_name, false).has_value(); }

Result<fs::path> FontLibrary::resolve_impl(const std::string& name, bool fallback, int charset) const {
    const auto key = std::make_tuple(name, fallback, charset);
    if (const auto it = impl_->resolutions.find(key); it != impl_->resolutions.end()) return it->second;
    auto result = resolve_uncached(name, fallback, charset);
    impl_->resolutions.emplace(key, result);
    return result;
}

Result<fs::path> FontLibrary::resolve_uncached(const std::string& cp932_name, bool fallback, int charset) const {
    const auto utf8_name = lower(to_utf8(cp932_to_utf32(cp932_name)));
    if (const auto it = impl_->registered.find(utf8_name); it != impl_->registered.end()) return it->second;
    if (const auto it = impl_->registered.find(lower(cp932_name)); it != impl_->registered.end()) return it->second;
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
            if (fs::path p = dir / file; fs::is_regular_file(p, ec)) return p;
            for (fs::recursive_directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
                 it.increment(ec)) {
                if (it.depth() > 3) it.disable_recursion_pending();
                if (it->is_regular_file(ec) && lower(it->path().filename().string()) == want) return it->path();
            }
        }
        return {};
    };

    // OBGI_FONT=<file name or path> replaces every font the scripts name (e.g. msyh.ttc for a Chinese build)
    if (fallback)
        if (const char* over = std::getenv("OBGI_FONT")) {
            std::error_code ec;
            if (fs::path direct(over); fs::is_regular_file(direct, ec)) return direct;
            if (fs::path p = find(over); !p.empty()) return p;
        }
    const std::string utf8 = to_utf8(cp932_to_utf32(cp932_name));
    // Shared Han characters exist in Japanese faces too, so per-glyph fallback
    // cannot select simplified Chinese forms. Prefer an SC face for ordinary
    // script fonts, even when the script did not register a Chinese charset.
    if (fallback && charset != 0x88) {
        for (const char* file : {"msyh.ttc", "simsun.ttc", "simhei.ttf", "NotoSansSC-VF.ttf", "NotoSansSC-Regular.otf", "NotoSerifSC-VF.ttf"})
            if (fs::path p = find(file); !p.empty()) return p;
    }
    if (fallback && charset == 0x88) {
        for (const char* file : {"msjh.ttc", "mingliu.ttc", "NotoSansTC-VF.ttf"})
            if (fs::path p = find(file); !p.empty()) return p;
    }
    for (const std::string& file : candidates_for(utf8))
        if (fs::path p = find(file); !p.empty()) return p;
    if (!fallback) return fail(Errc::Io, "font not installed: " + utf8);
    for (const char* file : kFallbacks)
        if (fs::path p = find(file); !p.empty()) return p;
    return fail(Errc::Io, "no usable font found for \"" + utf8 + "\"; set OBGI_FONT_DIR to a folder with a Japanese font");
}

Result<std::shared_ptr<Font>> FontLibrary::open(const std::string& cp932_name, int pixel_size, int charset) {
    const auto started = std::chrono::steady_clock::now();
    if (pixel_size < 4 || pixel_size > 512) return fail(Errc::Range, "font size out of range");
    auto path = resolve_impl(cp932_name, true, charset);
    if (!path) return std::unexpected(path.error());
    auto font = open_path(*impl_, *path, pixel_size);
    if (!font) return font;
    if ((*font)->impl_->fallbacks_ready) return font;
    // code points the named font lacks (simplified Chinese in a Japanese face, say) come from the first installed font that has them
    std::vector<Font::Impl::Fallback> chain;
    for (const char* file : kGlyphFallbacks) {
        auto p = resolve_impl(file, false);
        if (!p || *p == *path) continue;
        chain.push_back({*p, {}, false});
    }
    if (std::getenv("OBGI_DBGFONT")) std::fprintf(stderr, "font %s: %zu lazy fallbacks, open %lld ms\n", path->string().c_str(), chain.size(),
        static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()));
    (*font)->impl_->fallbacks = std::move(chain);
    (*font)->impl_->open_fallback = [state = impl_, pixel_size](const fs::path& fallback_path) -> std::shared_ptr<Font> {
        auto result = open_path(*state, fallback_path, pixel_size, false);
        return result ? *result : nullptr;
    };
    (*font)->impl_->fallbacks_ready = true;
    return font;
}

Result<std::shared_ptr<Font>> FontLibrary::open_path(Impl& m, const fs::path& path_in, int pixel_size, bool cache) {
    const fs::path* path = &path_in;
    const auto key = std::make_pair(*path, pixel_size);
    if (cache) if (auto it = m.fonts.find(key); it != m.fonts.end()) {
        it->second.used = ++m.font_clock;
        return it->second.font;
    }

    FileData data;
    if (auto it = m.files.find(*path); it != m.files.end()) {
        data = it->second;
    } else {
        auto mapping = MappedFile::open(*path);
        if (!mapping) return std::unexpected(mapping.error());
        data = std::make_shared<FontData>();
        data->mapped = std::move(*mapping);
        m.files.emplace(*path, data);
    }
    if (data->size() > static_cast<std::size_t>(std::numeric_limits<FT_Long>::max())) return fail(Errc::Range, "font is too large: " + path->string());

    std::shared_ptr<Font> font(new Font);
    font->impl_ = std::make_unique<Font::Impl>();
    font->impl_->data = data;
    font->impl_->engine = m.engine;
    const auto face_index = m.face_indices.contains(*path) ? m.face_indices.at(*path) : 0;
    if (FT_New_Memory_Face(m.ft, data->data(), static_cast<FT_Long>(data->size()), face_index, &font->impl_->face) != 0)
        return fail(Errc::Format, "FreeType cannot open " + path->string());
    FT_Face face = font->impl_->face;
    if (FT_Select_Charmap(face, FT_ENCODING_UNICODE) != 0) return fail(Errc::Format, "font has no Unicode map: " + path->string());
    if (FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixel_size)) != 0) return fail(Errc::Format, "font size not available");
    const FT_Size_Metrics& sm = face->size->metrics;
    font->ascent_ = static_cast<int>((sm.ascender + 63) >> 6);
    font->descent_ = static_cast<int>((-sm.descender + 63) >> 6);
    font->line_height_ = std::max(static_cast<int>((sm.height + 63) >> 6), font->ascent_ + font->descent_);
    if (cache) {
        if (m.fonts.size() >= 64) {
            const auto oldest = std::min_element(m.fonts.begin(), m.fonts.end(), [](const auto& a, const auto& b) {
                return a.second.used < b.second.used;
            });
            m.fonts.erase(oldest);
        }
        m.fonts.emplace(key, Impl::CachedFont{font, ++m.font_clock});
    }
    return font;
}

} // namespace obgi
