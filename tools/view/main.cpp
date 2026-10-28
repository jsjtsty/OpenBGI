// obgi_view: shows one CBG image or a BM frame sequence from a game archive.
//   obgi_view <game dir> <archive> <member> [--driver name] [--frame N] [--shot out.ppm]
// --shot renders one frame offscreen and writes it as a PPM instead of opening a window.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#include "audio/device.hpp"
#include "formats/bm.hpp"
#include "formats/cbg.hpp"
#include "render/renderer.hpp"
#include "vfs/arc.hpp"
#include "video/movie.hpp"
#include "video/player.hpp"

namespace {

int die(const std::string& msg) {
    std::fprintf(stderr, "obgi_view: %s\n", msg.c_str());
    return 1;
}

constexpr std::uint32_t kEsc = 27; // SDLK_ESCAPE

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> pos;
    const char* driver = nullptr;
    const char* shot = nullptr;
    long frame = 0;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--driver" && i + 1 < argc) driver = argv[++i];
        else if (a == "--shot" && i + 1 < argc) shot = argv[++i];
        else if (a == "--frame" && i + 1 < argc) frame = std::atol(argv[++i]);
        else pos.push_back(a);
    }
    if (pos.size() != 3) return die("usage: obgi_view <game dir> <archive> <member> [--driver d] [--frame N] [--shot out.ppm]");

    namespace fs = std::filesystem;
    auto arc = obgi::Arc::open(fs::path(std::u8string(pos[0].begin(), pos[0].end())) / fs::path(std::u8string(pos[1].begin(), pos[1].end())));
    if (!arc) return die(arc.error().message);
    const obgi::ArcEntry* entry = arc->find(pos[2]);
    if (!entry) return die("no member " + pos[2] + " in " + pos[1]);
    obgi::ByteView data = arc->data(*entry);

    obgi::RendererOptions ro;
    ro.headless = shot != nullptr;
    ro.driver = driver;
    ro.title = "OpenBGI viewer";
    auto renderer = obgi::Renderer::create(ro);
    if (!renderer) return die(renderer.error().message);
    std::printf("backend: %s\n", (*renderer)->driver_name());

    if (std::getenv("OBGI_DUMP_HEAD")) { for (int i = 0; i < 32; ++i) std::printf("%02x ", data[i]); std::printf("size=%zu\n", data.size()); }
    // .mpg: MPEG video through FFmpeg. --shot decodes up to --frame and saves it; otherwise play with sound.
    if (pos[2].size() > 4 && pos[2].compare(pos[2].size() - 4, 4, ".mpg") == 0) {
        if (shot) {
            auto dec = obgi::MovieDecoder::open(data, pos[2]);
            if (!dec) return die(dec.error().message);
            obgi::MovieDecoder::Frame f;
            for (long i = 0; i <= frame; ++i) {
                auto more = (*dec)->next(f);
                if (!more) return die(more.error().message);
                if (!*more) return die("movie has fewer than " + std::to_string(frame + 1) + " frames");
            }
            auto tex = (*renderer)->create_texture((*dec)->width(), (*dec)->height(), obgi::PixelFormat::Bgra8, f.bgra);
            if (!tex) return die(tex.error().message);
            const float s = std::min(800.0f / float((*dec)->width()), 600.0f / float((*dec)->height()));
            obgi::DrawParams dp;
            dp.w = float((*dec)->width()) * s, dp.h = float((*dec)->height()) * s;
            dp.x = (800.0f - dp.w) * 0.5f, dp.y = (600.0f - dp.h) * 0.5f;
            const float black[4] = {0, 0, 0, 1};
            if (auto r = (*renderer)->begin_frame(black); !r) return die(r.error().message);
            (*renderer)->draw(**tex, dp);
            if (auto r = (*renderer)->end_frame(); !r) return die(r.error().message);
            auto px = (*renderer)->read_back();
            if (!px) return die(px.error().message);
            std::FILE* out = std::fopen(shot, "wb");
            if (!out) return die(std::string("cannot write ") + shot);
            std::fprintf(out, "P6\n800 600\n255\n");
            for (std::size_t i = 0; i < px->size(); i += 4) std::fwrite(px->data() + i, 1, 3, out);
            std::fclose(out);
            std::printf("wrote %s (%ux%u, %.3f fps, frame %ld pts %.3f)\n", shot, (*dec)->width(), (*dec)->height(), (*dec)->fps(), frame, f.pts);
            return 0;
        }
        obgi::Mixer mixer;
        auto device = obgi::AudioDevice::open(mixer);
        if (!device) std::fprintf(stderr, "no audio device (%s): playing silent\n", device.error().message.c_str());
        auto end = obgi::play_movie(**renderer, mixer, data, pos[2]);
        if (!end) return die(end.error().message);
        return 0;
    }

    // Either a still image (cbg) or a movie (bm); both end up as an RGBA/BGRA texture per frame.
    obgi::CbgImage still;
    obgi::Result<obgi::BmDecoder> movie = obgi::fail(obgi::Errc::Format, "not a movie");
    const bool is_still = obgi::is_cbg(data);
    std::uint32_t img_w = 0, img_h = 0, frames = 1;
    if (is_still) {
        auto img = obgi::cbg_decode(data);
        if (!img) return die(img.error().message);
        still = std::move(*img);
        img_w = still.width, img_h = still.height;
    } else {
        movie = obgi::BmDecoder::open(data);
        if (!movie) return die("not a CBG image or BM movie: " + movie.error().message);
        img_w = movie->width(), img_h = movie->height(), frames = movie->frame_count();
    }

    std::vector<std::uint8_t> rgba;
    std::uint32_t next_index = 0; // next BM frame the decoder has not consumed
    auto make_frame = [&](std::uint32_t index) -> obgi::Result<std::unique_ptr<obgi::Texture>> {
        if (is_still) {
            rgba = still.to_rgba();
            return (*renderer)->create_texture(img_w, img_h, obgi::PixelFormat::Rgba8, rgba);
        }
        // Frames are deltas: decode forward from the keyframe 0 up to the one asked for.
        if (index < next_index) next_index = 0;
        while (next_index <= index) {
            if (auto r = movie->decode_frame(next_index++); !r) return std::unexpected(r.error());
        }
        obgi::ByteView canvas = movie->canvas();
        rgba.assign(canvas.begin(), canvas.end());
        if (movie->source_bits() != 32)
            for (std::size_t i = 3; i < rgba.size(); i += 4) rgba[i] = 255;
        return (*renderer)->create_texture(img_w, img_h, obgi::PixelFormat::Bgra8, rgba);
    };

    // Fit the image inside the 800x600 canvas, keeping its aspect ratio.
    const float scale = std::min(800.0f / float(img_w), 600.0f / float(img_h));
    obgi::DrawParams dp;
    dp.w = float(img_w) * scale;
    dp.h = float(img_h) * scale;
    dp.x = (800.0f - dp.w) * 0.5f;
    dp.y = (600.0f - dp.h) * 0.5f;
    const float clear[4] = {0.18f, 0.18f, 0.2f, 1.0f};

    auto render_one = [&](const obgi::Texture& tex) -> obgi::Result<void> {
        if (auto r = (*renderer)->begin_frame(clear); !r) return r;
        (*renderer)->draw(tex, dp);
        return (*renderer)->end_frame();
    };

    if (shot) {
        auto tex = make_frame(static_cast<std::uint32_t>(std::min<long>(frame, long(frames) - 1)));
        if (!tex) return die(tex.error().message);
        if (auto r = render_one(**tex); !r) return die(r.error().message);
        auto px = (*renderer)->read_back();
        if (!px) return die(px.error().message);
        std::FILE* f = std::fopen(shot, "wb");
        if (!f) return die(std::string("cannot write ") + shot);
        std::fprintf(f, "P6\n800 600\n255\n");
        for (std::size_t i = 0; i < px->size(); i += 4) std::fwrite(px->data() + i, 1, 3, f);
        std::fclose(f);
        std::printf("wrote %s (%ux%u image, frame %ld/%u)\n", shot, img_w, img_h, frame, frames);
        return 0;
    }

    using clock = std::chrono::steady_clock;
    // Placeholder: the real BM timing is not known yet (header field 0x24 is unconfirmed, see
    // analysis/Codex任务书4.md). The frame shown is derived from elapsed time so a slow frame drops
    // frames instead of slowing the movie down.
    constexpr double fps = 30.0;
    const std::uint32_t first_index = static_cast<std::uint32_t>(std::max<long>(frame, 0)) % frames;
    const auto start = clock::now();
    std::unique_ptr<obgi::Texture> tex;
    long shown = -1;
    while ((*renderer)->poll_events()) {
        if ((*renderer)->take_key() == kEsc) break;
        std::uint32_t index = first_index;
        if (!is_still) {
            const double t = std::chrono::duration<double>(clock::now() - start).count();
            index = static_cast<std::uint32_t>((first_index + static_cast<std::uint64_t>(t * fps)) % frames);
        }
        if (shown != long(index)) {
            auto t = make_frame(index);
            if (!t) return die(t.error().message);
            tex = std::move(*t);
            shown = long(index);
        }
        if (auto r = render_one(*tex); !r) return die(r.error().message);
        if (is_still) std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    tex.reset(); // before the renderer goes away
    return 0;
}
