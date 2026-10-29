#include "bindings/particle_subops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <optional>
#include <random>

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

// The particle engine of the reference engine (src/gfx/particle/*.c, docs/particle_rain.md), ported: one engine per particle screen
// (C0 00), 8 screens, patterns of two particle types shared by every screen, a wind, a camera. The screen draws its particles into an
// image slot that a sprite shows.

constexpr int kScreens = 8, kPatterns = 64, kScales = 32, kMaxParticles = 0x8000;
constexpr Word kHandleTag = 0xC0000000u, kSurfaceSlot = 0x7E800000u, kPatternSlot = 0x7EA00000u;
constexpr std::uint32_t kNoImage = 0xFFFFFFFFu;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

struct Rng {
    std::mt19937 gen{12345};
    // ((rand << 15) | rand) % |n + 1|: 0 .. n for n >= 0
    std::int32_t rnd(std::int32_t n) {
        const std::int32_t r = static_cast<std::int32_t>(((gen() & 0x7FFF) << 15) | (gen() & 0x7FFF));
        std::int32_t m = n + 1;
        if (m < 0) m = -m;
        return m ? r % m : 0;
    }
};

struct PatternA { bool defined = false; std::int32_t x_spread = 0, y_start = 0, z_spread = 0, vx_base = 0, vx_var = 0, vy_base = 0, vy_var = 0, vz_base = 0, vz_var = 0, wind = 0; };
struct PatternB {
    bool defined = false;
    std::int32_t life_base = 0, life_var = 0, x_spread = 0, y_start = 0, z_spread = 0, vx_base = 0, vx_var = 0, vy_base = 0, vy_var = 0, vz_base = 0, vz_var = 0;
    std::int32_t turn_base = 0, turn_var = 0, wind = 0, fade_in = 1, fade_out = 1, effect = 0;
};

// An animation (or the picture) of a pattern: for each of the 32 scales a list of slot ids holding the scaled frame(s).
struct Animation {
    int count = 0;
    std::array<std::vector<Word>, kScales> frames; // per scale, `count` slot ids
    std::int32_t rate1 = 0, rate2 = 0;             // 16.16 frames per tick
};

struct Patterns {
    std::array<PatternA, kPatterns> a;
    std::array<PatternB, kPatterns> b;
    std::array<std::array<std::vector<Word>, kScales>, kPatterns> picture[2]; // [type][pattern][scale]: one slot (or none)
    std::array<Animation, kPatterns> anim[2];
    std::array<Animation, kPatterns> alt;
    std::array<std::int32_t, kPatterns> alt_level{};
    std::array<std::array<std::pair<std::uint32_t, std::uint32_t>, kScales>, kPatterns> size[2]; // scaled picture sizes
    Word next_slot = 0;
    int anim_mode = 0; // C0 1F
};

struct Particle {
    int type = 0, pattern = 0;
    bool alive = false;
    std::int32_t x = 0, y = 0, z = 0, wind_weight = 0, effect = 0x20;
    std::int32_t frame_pos = 0, frame_rate = 0, alt_pos = 0, alt_rate = 0;
    std::int32_t vx = 0, vy = 0, vz = 0;                                          // type 0; the current key for type 1
    std::uint32_t life = 0, age = 0, turn = 1, t = 0, fade_in = 1, fade_out = 1; // type 1
    std::int32_t vx1 = 0, vy1 = 0, vz1 = 0;
};

struct Wind {
    std::uint32_t interval = 0;
    bool enabled = false;
    std::int32_t cur[3] = {}, base[3] = {}, var[3] = {};
    std::uint32_t hold_ms = 0, hold_var_ms = 0, move_ms = 0, move_var_ms = 0, hold_t = 0, hold_var_t = 0, move_t = 0, move_var_t = 0;
    int phase = 0;
    std::uint32_t len = 1, pos = 0;
    std::int32_t from[3] = {}, to[3] = {};
};

struct Screen {
    bool used = false, visible = false;
    std::uint32_t w = 0, h = 0;
    std::int32_t cx = 0, cy = 0;
    std::int32_t x = 0, y = 0, effect = 1, level = 0;
    std::uint32_t prio = 0;
    std::uint32_t interval = 10;   // tick ms, 0 = paused
    std::int64_t last_tick = 0;
    std::vector<std::optional<Particle>> slots;
    std::int32_t max_count[2][kPatterns] = {}, live_count[2][kPatterns] = {};
    std::uint32_t spawn_every[2][kPatterns] = {};
    std::int64_t next_spawn[2][kPatterns] = {};
    std::int32_t cam[3] = {};
    double sin_a = 0, cos_a = 1, sin_b = 0, cos_b = 1, sin_c = 0, cos_c = 1;
    std::int32_t dist = 0;
    Wind wind;
    std::uint32_t auto_interval = 0;
    std::int64_t auto_next = 0;
};

struct World {
    Patterns patterns;
    std::array<Screen, kScreens> screens;
    Rng rng;
    Display* display = nullptr;
};

// ---- the wind ----

void wind_pick(World& w, const Wind& wd, std::int32_t out[3]) {
    for (int i = 0; i < 3; ++i) out[i] = w.rng.rnd(2 * wd.var[i]) + wd.base[i] - wd.var[i];
}
void wind_phase(Wind& wd, int phase, std::uint32_t len) { wd.phase = phase, wd.pos = 0, wd.len = len ? len : 1; }
void wind_next(World& w, Wind& wd, bool first) {
    wind_phase(wd, 0, wd.hold_t + static_cast<std::uint32_t>(w.rng.rnd(static_cast<std::int32_t>(wd.hold_var_t))));
    if (first) {
        wind_pick(w, wd, wd.from);
        wind_pick(w, wd, wd.to);
    } else {
        std::copy(wd.to, wd.to + 3, wd.from);
        wind_pick(w, wd, wd.to);
    }
}
bool wind_recalc(Wind& wd) {
    if (wd.interval == 0) return false;
    wd.hold_t = wd.hold_ms / wd.interval, wd.hold_var_t = wd.hold_var_ms / wd.interval;
    wd.move_t = wd.move_ms / wd.interval, wd.move_var_t = wd.move_var_ms / wd.interval;
    return true;
}
void wind_step(World& w, Wind& wd) {
    if (!wd.enabled) { std::fill(wd.cur, wd.cur + 3, 0); return; }
    if (wd.phase == 0) std::copy(wd.from, wd.from + 3, wd.cur);
    else {
        const auto pos = static_cast<std::int32_t>(wd.pos), rest = static_cast<std::int32_t>(wd.len - wd.pos), len = static_cast<std::int32_t>(wd.len);
        for (int i = 0; i < 3; ++i) wd.cur[i] = (wd.to[i] * pos + wd.from[i] * rest) / len;
    }
    if (++wd.pos < wd.len) return;
    if (wd.phase == 0) wind_phase(wd, 1, wd.move_t + static_cast<std::uint32_t>(w.rng.rnd(static_cast<std::int32_t>(wd.move_var_t))));
    else wind_next(w, wd, false);
}

// ---- patterns: scaled pictures kept in image slots ----

struct Image { std::uint32_t w = 0, h = 0; std::vector<std::uint32_t> px; };

// Scales a BGRA image by s / 65536 (area average, alpha weighted).
Image scale_image(const Image& src, std::int32_t s16, bool argb) {
    Image out;
    out.w = static_cast<std::uint32_t>((std::uint64_t{src.w} * static_cast<std::uint32_t>(s16)) >> 16);
    out.h = static_cast<std::uint32_t>((std::uint64_t{src.h} * static_cast<std::uint32_t>(s16)) >> 16);
    if (out.w == 0 || out.h == 0) return out;
    out.px.assign(std::size_t(out.w) * out.h, 0);
    for (std::uint32_t y = 0; y < out.h; ++y) {
        const std::uint32_t y0 = std::uint32_t(std::uint64_t{y} * src.h / out.h), y1 = std::max(y0 + 1, std::uint32_t(std::uint64_t{y + 1} * src.h / out.h));
        for (std::uint32_t x = 0; x < out.w; ++x) {
            const std::uint32_t x0 = std::uint32_t(std::uint64_t{x} * src.w / out.w), x1 = std::max(x0 + 1, std::uint32_t(std::uint64_t{x + 1} * src.w / out.w));
            std::uint64_t sb = 0, sg = 0, sr = 0, sa = 0, n = 0;
            for (std::uint32_t yy = y0; yy < y1 && yy < src.h; ++yy)
                for (std::uint32_t xx = x0; xx < x1 && xx < src.w; ++xx) {
                    const std::uint32_t p = src.px[std::size_t(yy) * src.w + xx];
                    const std::uint32_t a = argb ? p >> 24 : 255;
                    sb += std::uint64_t{p & 0xFF} * a, sg += std::uint64_t{(p >> 8) & 0xFF} * a, sr += std::uint64_t{(p >> 16) & 0xFF} * a, sa += a, ++n;
                }
            if (n == 0) continue;
            const std::uint32_t a = argb ? std::uint32_t(sa / n) : 0;
            const std::uint32_t b = sa ? std::uint32_t(sb / sa) : 0, g = sa ? std::uint32_t(sg / sa) : 0, r = sa ? std::uint32_t(sr / sa) : 0;
            out.px[std::size_t(y) * out.w + x] = b | g << 8 | r << 16 | a << 24;
        }
    }
    return out;
}

void free_slots(World& w, std::vector<Word>& ids) {
    for (Word id : ids) w.display->free_slot(id);
    ids.clear();
}

// Makes the 32 scaled copies of slot `bmp` into slots; `out[k]` receives the slot id of scale k (0 when it came out empty).
bool make_scales(World& w, Word bmp, std::array<std::vector<Word>, kScales>& out, std::array<std::pair<std::uint32_t, std::uint32_t>, kScales>* sizes) {
    const SlotInfo* si = w.display->slot(bmp);
    if (!si || (si->format != SlotFormat::Bgrx && si->format != SlotFormat::Bgra)) return false;
    const bool argb = si->format == SlotFormat::Bgra;
    auto px = w.display->read_slot(bmp);
    if (!px) return false;
    Image src{si->width, si->height, std::move(*px)};
    for (int k = 0; k < kScales; ++k) {
        free_slots(w, out[std::size_t(k)]);
        const std::int32_t s = (0x200000 - k * 0x10000) / 32;
        Image sc = scale_image(src, s, argb);
        if (sizes) (*sizes)[std::size_t(k)] = {sc.w, sc.h};
        if (sc.px.empty()) continue;
        const Word id = kPatternSlot + w.patterns.next_slot++;
        if (!w.display->upload_slot(id, sc.w, sc.h, SlotFormat::Bgra, sc.px)) continue;
        out[std::size_t(k)].push_back(id);
    }
    return true;
}

// ---- particles ----

Particle new_particle(World& w, int type, int pattern) {
    Particle p;
    p.type = type, p.pattern = pattern;
    Patterns& pt = w.patterns;
    const Animation& an = pt.anim[type][std::size_t(pattern)];
    p.frame_rate = an.count > 0 ? an.rate1 + w.rng.rnd(an.rate2) : 0;
    if (type == 1) { const Animation& al = pt.alt[std::size_t(pattern)]; p.alt_rate = al.count > 0 ? al.rate1 + w.rng.rnd(al.rate2) : 0; }
    Rng& r = w.rng;
    if (type == 0) {
        const PatternA& d = pt.a[std::size_t(pattern)];
        if (!d.defined) return p;
        p.x = r.rnd(2 * d.x_spread) - d.x_spread, p.y = d.y_start, p.z = r.rnd(2 * d.z_spread) - d.z_spread;
        p.vx = d.vx_base - d.vx_var + r.rnd(2 * d.vx_var);
        p.vy = d.vy_base - d.vy_var + r.rnd(2 * d.vy_var);
        p.vz = d.vz_base - d.vz_var + r.rnd(2 * d.vz_var);
        p.wind_weight = d.wind, p.effect = 0x20, p.alive = true;
    } else {
        const PatternB& d = pt.b[std::size_t(pattern)];
        if (!d.defined) return p;
        p.life = static_cast<std::uint32_t>(d.life_base + r.rnd(d.life_var));
        p.x = r.rnd(2 * d.x_spread) - d.x_spread, p.y = d.y_start, p.z = r.rnd(2 * d.z_spread) - d.z_spread;
        p.vx = d.vx_base - d.vx_var + r.rnd(2 * d.vx_var);
        p.vy = d.vy_base - d.vy_var + r.rnd(2 * d.vy_var);
        p.vz = d.vz_base - d.vz_var + r.rnd(2 * d.vz_var);
        p.vx1 = d.vx_base - d.vx_var + r.rnd(2 * d.vx_var);
        p.vy1 = d.vy_base - d.vy_var + r.rnd(2 * d.vy_var);
        p.vz1 = d.vz_base - d.vz_var + r.rnd(2 * d.vz_var);
        p.turn = static_cast<std::uint32_t>(d.turn_base + r.rnd(d.turn_var));
        if (p.turn == 0) p.turn = 1;
        p.t = 0, p.wind_weight = d.wind, p.fade_in = static_cast<std::uint32_t>(d.fade_in), p.fade_out = static_cast<std::uint32_t>(d.fade_out);
        p.effect = d.effect, p.alive = true;
    }
    return p;
}

void step_frames(Patterns& pt, Particle& p) {
    auto advance = [](std::int32_t& pos, std::int32_t rate, int count) {
        if (count <= 0) return;
        pos += rate;
        while (static_cast<std::uint32_t>(pos) >= (static_cast<std::uint32_t>(count) << 16)) pos -= static_cast<std::int32_t>(static_cast<std::uint32_t>(count) << 16);
    };
    advance(p.frame_pos, p.frame_rate, pt.anim[p.type][std::size_t(p.pattern)].count);
    if (p.type == 1) advance(p.alt_pos, p.alt_rate, pt.alt[std::size_t(p.pattern)].count);
}

// one tick of motion; false once dead
bool step_particle(World& w, Particle& p) {
    if (!p.alive) return false;
    if (p.type == 0) {
        p.x += p.vx, p.y += p.vy, p.z += p.vz;
        step_frames(w.patterns, p);
        return true;
    }
    const std::uint32_t age = p.age++;
    if (age >= p.life) return false;
    step_frames(w.patterns, p);
    p.x += (p.vx1 - p.vx) * static_cast<std::int32_t>(p.t) / static_cast<std::int32_t>(p.turn) + p.vx;
    p.y += (p.vy1 - p.vy) * static_cast<std::int32_t>(p.t) / static_cast<std::int32_t>(p.turn) + p.vy;
    p.z += (p.vz1 - p.vz) * static_cast<std::int32_t>(p.t) / static_cast<std::int32_t>(p.turn) + p.vz;
    if (p.t < p.turn) { ++p.t; return true; }
    const PatternB& d = w.patterns.b[std::size_t(p.pattern)];
    p.vx = p.vx1, p.vy = p.vy1, p.vz = p.vz1;
    p.vx1 = d.vx_base - d.vx_var + w.rng.rnd(2 * d.vx_var);
    p.vy1 = d.vy_base - d.vy_var + w.rng.rnd(2 * d.vy_var);
    p.vz1 = d.vz_base - d.vz_var + w.rng.rnd(2 * d.vz_var);
    p.turn = static_cast<std::uint32_t>(d.turn_base + w.rng.rnd(d.turn_var));
    if (p.turn == 0) p.turn = 1;
    p.t = 0;
    return true;
}

// transparency 0..0x100 the particle is drawn with
int particle_level(const Particle& p) {
    if (p.type == 0) return 0;
    const std::uint32_t left = p.life - p.age;
    if (!p.alive) return 0;
    if (left < p.fade_out) return 0x100 - static_cast<int>((left << 8) / p.fade_out);
    if (p.age < p.fade_in) return 0x100 - static_cast<int>((p.age << 8) / p.fade_in);
    return 0;
}

// the slot holding the particle's picture at a scale index (the current animation frame when the pattern has one); 0xFFFFFFFF none
Word particle_picture(const Patterns& pt, const Particle& p, int index) {
    if (index < 0 || index >= kScales) return kNoImage;
    const Animation& an = pt.anim[p.type][std::size_t(p.pattern)];
    const auto& pic = pt.picture[p.type][std::size_t(p.pattern)][std::size_t(index)];
    if (an.count > 0) {
        const auto& list = an.frames[std::size_t(index)];
        if (list.empty()) return kNoImage;
        return list[static_cast<std::uint32_t>(p.frame_pos >> 16) % static_cast<std::uint32_t>(list.size())];
    }
    return pic.empty() ? kNoImage : pic[0];
}

// ---- the engine ----

bool tick(World& w, Screen& s, bool& in_sync, std::int64_t now) {
    if (s.interval == 0) return false;
    in_sync = !(s.last_tick + 500 < now);
    if (!in_sync) s.last_tick = now;
    if (s.last_tick > now) return true;
    do {
        std::int32_t wv[3];
        wind_step(w, s.wind);
        if (s.wind.enabled) std::copy(s.wind.cur, s.wind.cur + 3, wv); else std::fill(wv, wv + 3, 0);
        for (auto& slot : s.slots) {
            if (!slot) continue;
            Particle& q = *slot;
            if (q.alive) {
                q.x += (q.wind_weight * wv[0]) >> 8, q.y += (q.wind_weight * wv[1]) >> 8, q.z += (q.wind_weight * wv[2]) >> 8;
            }
            const bool alive = step_particle(w, q);
            const std::int32_t x = q.x >> 8, y = q.y >> 8, z = q.z >> 8;
            if (alive && x <= 16000 && x >= -16000 && y <= 16000 && y >= 0 && z <= 8000 && z >= -8000) continue;
            --s.live_count[q.type][q.pattern];
            slot.reset();
        }
        s.last_tick += s.interval;
    } while (s.last_tick <= now);
    return true;
}

void spawn(World& w, Screen& s, int type, int pattern, std::int64_t now, bool reset) {
    std::int64_t& next = s.next_spawn[type][pattern];
    if (reset) next = now;
    while (next <= now) {
        if (s.live_count[type][pattern] >= s.max_count[type][pattern]) { next = 0; return; }
        Particle q = new_particle(w, type, pattern);
        auto it = std::find_if(s.slots.begin(), s.slots.end(), [](const auto& o) { return !o; });
        if (it != s.slots.end()) *it = q;
        ++s.live_count[type][pattern];
        if (next == 0) next = now;
        next += s.spawn_every[type][pattern];
        if (s.spawn_every[type][pattern] == 0) { next = now + 1; break; }
    }
}

void update_to(World& w, Screen& s, std::int64_t now) {
    bool in_sync = true;
    if (!tick(w, s, in_sync, now)) return;
    for (int t = 0; t < 2; ++t)
        for (int p = 0; p < kPatterns; ++p) spawn(w, s, t, p, now, !in_sync);
}

void render(World& w, Screen& s, std::uint32_t surface) {
    Display& d = *w.display;
    d.fill_slot(surface, 0);
    struct Vis { std::int32_t x, y, z; const Particle* q; };
    std::vector<Vis> vis;
    for (const auto& slot : s.slots) {
        if (!slot) continue;
        std::int32_t x = slot->x - s.cam[0], y = slot->y - s.cam[1], z = slot->z - s.cam[2], t;
        t = static_cast<std::int32_t>(double(y) * s.cos_a - double(z) * s.sin_a);
        z = static_cast<std::int32_t>(double(y) * s.sin_a + double(z) * s.cos_a), y = t;
        t = static_cast<std::int32_t>(double(z) * s.cos_b - double(x) * s.sin_b);
        x = static_cast<std::int32_t>(double(x) * s.cos_b + double(z) * s.sin_b), z = t;
        t = static_cast<std::int32_t>(double(x) * s.cos_c - double(y) * s.sin_c);
        y = static_cast<std::int32_t>(double(x) * s.sin_c + double(y) * s.cos_c), x = t;
        if (z < 0) continue;
        vis.push_back({x, y, z, &*slot});
    }
    std::stable_sort(vis.begin(), vis.end(), [](const Vis& a, const Vis& b) { return a.z > b.z; });
    for (const Vis& v : vis) {
        const std::int32_t den = (s.dist << 8) + v.z;
        if (den == 0) continue;
        const int idx = 32 - (s.dist * 32) / ((v.z >> 8) + s.dist);
        const Word pic = particle_picture(w.patterns, *v.q, idx);
        if (pic == kNoImage) continue;
        const SlotInfo* pi = d.slot(pic);
        if (!pi) continue;
        const std::int32_t sx = s.cx + static_cast<std::int32_t>(static_cast<std::uint32_t>(v.x) * static_cast<std::uint32_t>(s.dist)) / den - static_cast<std::int32_t>(pi->width >> 1);
        const std::int32_t sy = s.cy - static_cast<std::int32_t>(static_cast<std::uint32_t>(v.y) * static_cast<std::uint32_t>(s.dist)) / den - static_cast<std::int32_t>(pi->height >> 1);
        const std::uint32_t mode = blend_mode_supported(static_cast<std::uint32_t>(v.q->effect)) ? static_cast<std::uint32_t>(v.q->effect) : 1;
        (void)d.blit(surface, sx, sy, pic, 0, 0, pi->width, pi->height, mode, static_cast<std::uint32_t>(particle_level(*v.q)));
    }
}

// ---- rain screens (C0 40..4F; the reference engine's gfx/rain.c) ----

constexpr int kRains = 8;
constexpr Word kRainTag = 0xC1000000u, kRainSurface = 0x7E900000u;

struct Drop { std::int32_t tail[3], head[3]; };

struct Rain {
    bool used = false, visible = false, started = false;
    std::uint32_t w = 0, h = 0;
    std::int32_t x = 0, y = 0, effect = 1, level = 0;
    std::uint32_t prio = 0;
    std::int32_t x_min = -6000, y_top = -4000, z_min = -2000, x_max = 6000, y_bottom = 4000, z_max = 2000;
    std::int32_t dir[3] = {0, -1, 0}, fall = 60 << 8, length = 350 << 8;
    std::uint32_t colour = 0xFFFFFFFFu, per_stage = 20, stage_ms = 50;
    std::int32_t cam[3] = {}, ang[3] = {}, dist = 100;
    std::int32_t mask = -1;
    std::int64_t last_time = 0;
    std::vector<Drop> drops;
};

struct RainWorld {
    std::array<Rain, kRains> rains;
    bool effects_enabled = true;
    std::uint32_t frame_ms = 50;
    std::int64_t next_frame = 0;
    Rng rng;
    Display* display = nullptr;
};

void rain_stage(RainWorld& rw, Rain& r) {
    const std::int32_t fall = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.fall) >> 8), len = static_cast<std::int32_t>(static_cast<std::uint32_t>(r.length) >> 8);
    std::int32_t step[3], streak[3];
    for (int k = 0; k < 3; ++k) step[k] = r.dir[k] * fall, streak[k] = r.dir[k] * len;
    std::erase_if(r.drops, [&](Drop& d) {
        for (int k = 0; k < 3; ++k) d.tail[k] += step[k], d.head[k] += step[k];
        return d.tail[1] <= r.y_bottom;
    });
    const std::uint32_t xr = static_cast<std::uint32_t>(r.x_max - r.x_min), zr = static_cast<std::uint32_t>(r.z_max - r.z_min);
    for (std::uint32_t i = 0; i < r.per_stage; ++i) {
        Drop d;
        d.head[0] = r.x_min + static_cast<std::int32_t>(xr ? std::uint32_t(rw.rng.gen() & 0x7FFF) % xr : 0);
        d.head[1] = r.y_top - static_cast<std::int32_t>(rw.rng.gen() % 100);
        d.head[2] = r.z_min + static_cast<std::int32_t>(zr ? std::uint32_t(rw.rng.gen() & 0x7FFF) % zr : 0);
        for (int k = 0; k < 3; ++k) d.tail[k] = d.head[k] - streak[k];
        r.drops.push_back(d);
    }
}

void rain_update(RainWorld& rw, Rain& r, std::int64_t now) {
    if (!r.started) return;
    std::int64_t elapsed = now - r.last_time;
    if (elapsed >= 500) { r.last_time = now; return; }
    if (elapsed < std::int64_t(r.stage_ms)) return;
    do { rain_stage(rw, r); elapsed -= r.stage_ms; } while (elapsed >= std::int64_t(r.stage_ms));
    r.last_time = now - elapsed;
}

void draw_line(std::vector<std::uint32_t>& px, int w, int h, std::uint32_t colour, int x0, int y0, int x1, int y1) {
    const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0), n = std::max(dx, dy);
    if ((x0 < 0 && x1 < 0) || (x0 >= w && x1 >= w) || (y0 < 0 && y1 < 0) || (y0 >= h && y1 >= h)) return;
    for (int i = 0; i <= n; ++i) {
        const int x = n ? x0 + (x1 - x0) * i / n : x0, y = n ? y0 + (y1 - y0) * i / n : y0;
        if (x >= 0 && x < w && y >= 0 && y < h) px[std::size_t(y) * std::size_t(w) + std::size_t(x)] = colour;
    }
}

void rain_redraw(Rain& r, Display& d, Word slot) {
    std::vector<std::uint32_t> px(std::size_t(r.w) * r.h, 0);
    auto angle = [](std::int32_t tenths, std::int32_t& s, std::int32_t& c) {
        const double rad = double(tenths) * 0.017453292519444445 * 0.1;
        s = static_cast<std::int32_t>(std::sin(rad) * 256.0), c = static_cast<std::int32_t>(std::cos(rad) * 256.0);
    };
    std::int32_t sa, ca, sb, cb, sc, cc;
    angle(r.ang[0], sa, ca), angle(r.ang[1], sb, cb), angle(r.ang[2], sc, cc);
    auto to_view = [&](const std::int32_t in[3], std::int32_t out[3]) {
        const std::int32_t x = in[0] - r.cam[0], y = in[1] - r.cam[1], z = in[2] - r.cam[2];
        const std::int32_t x1 = (cc * x - sc * y) >> 8, y1 = (sc * x1 + cc * y) >> 8;
        const std::int32_t y2 = (y1 * cb - z * sb) >> 8, z2 = (y1 * sb + z * cb) >> 8;
        out[0] = (z2 * sa + x1 * ca) >> 8, out[1] = y2, out[2] = (z2 * ca - x1 * sa) >> 8;
    };
    const int hw = int(r.w >> 1), hh = int(r.h >> 1);
    for (const Drop& dr : r.drops) {
        std::int32_t a[3], b[3];
        to_view(dr.tail, a);
        if (a[2] <= r.dist) continue;
        to_view(dr.head, b);
        if (b[2] <= r.dist) continue;
        a[0] = r.dist * a[0] / a[2], a[1] = r.dist * a[1] / a[2], b[0] = r.dist * b[0] / b[2], b[1] = r.dist * b[1] / b[2];
        const std::int32_t alpha = std::int32_t(r.colour >> 24), depth8 = a[2] / 8;
        std::int32_t lit = depth8 ? alpha * r.dist / depth8 : alpha;
        if (lit > alpha) lit = alpha;
        draw_line(px, int(r.w), int(r.h), (r.colour & 0x00FFFFFFu) | (std::uint32_t(lit) << 24), a[0] + hw, hh - a[1], b[0] + hw, hh - b[1]);
    }
    d.upload_slot(slot, r.w, r.h, SlotFormat::Bgra, px);
}

void sync_sprite(World& w, Word handle, Screen& s) {
    Sprite& sp = w.display->sprite(handle);
    sp.slot = kSurfaceSlot + (handle & 7), sp.x = s.x, sp.y = s.y, sp.blend = blend_mode_supported(std::uint32_t(s.effect)) ? std::uint32_t(s.effect) : 1;
    sp.alpha = std::uint32_t(s.level), sp.priority = s.prio, sp.visible = s.visible;
}

} // namespace

void register_particle_subops(vm::SubopRegistry& r, SystemState& state, Display* display) {
    if (!display) return;
    auto world = std::make_shared<World>();
    world->display = display;
    World* wp = world.get();
    // per frame: the simulation of every screen, and the automatic redraws
    state.per_frame.push_back([world, wp](std::int64_t now) { // `world` keeps the engine alive for every handler that holds `wp`
        for (int i = 0; i < kScreens; ++i) {
            Screen& s = wp->screens[std::size_t(i)];
            if (!s.used) continue;
            update_to(*wp, s, now);
            if (s.auto_interval && now >= s.auto_next && s.visible) {
                render(*wp, s, kSurfaceSlot + std::uint32_t(i));
                s.auto_next = now + s.auto_interval;
            }
        }
    });
    auto screen_of = [wp](Ctx& c, Word h) -> Screen* {
        const bool ok = (h & 0xFFFFFFF8u) == kHandleTag && wp->screens[h & 7].used;
        if (!ok) c.fail(Errc::Range, "no particle screen " + std::to_string(h));
        return ok ? &wp->screens[h & 7] : nullptr;
    };
    add(r, 0xC000, "create particle screen", 2, 1, [wp](Ctx& c) {
        const Word h = c.pop(), w = c.pop();
        if (!c.ok()) return 0;
        for (Word i = 0; i < kScreens; ++i) {
            Screen& s = wp->screens[i];
            if (s.used) continue;
            if (w == 0 || h == 0 || w > 0x2000 || h > 0x2000) { c.fail(Errc::Range, "bad effect screen size"); return 0; }
            s = Screen{};
            s.used = true, s.w = w, s.h = h, s.cx = std::int32_t(w >> 1), s.cy = std::int32_t(h >> 1);
            s.slots.assign(0x8000, std::nullopt);
            s.last_tick = c.vm.now_ms;
            s.wind.interval = 10;
            wp->display->create_slot(kSurfaceSlot + i, w, h, SlotFormat::Bgra);
            wp->display->fill_slot(kSurfaceSlot + i, 0);
            sync_sprite(*wp, kHandleTag | i, s);
            c.push(kHandleTag | i);
            return 0;
        }
        c.fail(Errc::Range, "no more particle screens");
        return 0;
    });
    add(r, 0xC001, "delete particle screen", 1, 0, [wp, screen_of](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Screen* s = screen_of(c, h);
        if (!s) return 0;
        wp->display->free_slot(kSurfaceSlot + (h & 7));
        wp->display->remove_sprite(h);
        *s = Screen{};
        return 0;
    });
    add(r, 0xC004, "show particle screen", 2, 0, [wp, screen_of](Ctx& c) {
        const Word on = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (Screen* s = screen_of(c, h)) { s->visible = on != 0; sync_sprite(*wp, h, *s); }
        return 0;
    });
    add(r, 0xC005, "place particle screen", 6, 0, [wp, screen_of](Ctx& c) {
        const Word prio = c.pop(), level = c.pop(), effect = c.pop();
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (level > 0x100 || prio > 0xFFFF) { c.fail(Errc::Range, "particle screen parameter out of range"); return 0; }
        if (Screen* s = screen_of(c, h)) {
            s->x = x, s->y = y, s->effect = std::int32_t(effect), s->level = std::int32_t(level), s->prio = prio;
            sync_sprite(*wp, h, *s);
        }
        return 0;
    });
    add(r, 0xC008, "redraw particle screen", 1, 0, [wp, screen_of](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (Screen* s = screen_of(c, h)) render(*wp, *s, kSurfaceSlot + (h & 7));
        return 0;
    });
    add(r, 0xC009, "auto redraw particle screen", 2, 0, [screen_of, world](Ctx& c) {
        const Word ms = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (Screen* s = screen_of(c, h)) s->auto_interval = ms, s->auto_next = c.vm.now_ms;
        return 0;
    });
    add(r, 0xC00A, "particle rectangle limit", 2, 0, [screen_of](Ctx& c) { c.pop(); const Word h = c.pop(); if (c.ok()) screen_of(c, h); return 0; });
    add(r, 0xC00B, "particle camera", 10, 0, [screen_of](Ctx& c) {
        const auto cy = static_cast<std::int32_t>(c.pop()), cx = static_cast<std::int32_t>(c.pop());
        const auto dist = static_cast<std::int32_t>(c.pop()), cc = static_cast<std::int32_t>(c.pop()), b = static_cast<std::int32_t>(c.pop()), a = static_cast<std::int32_t>(c.pop());
        const auto z = static_cast<std::int32_t>(c.pop()), y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Screen* s = screen_of(c, h);
        if (!s) return 0;
        s->cx = cx, s->cy = cy;
        if (dist <= 0) { c.fail(Errc::Range, "bad particle scale"); return 0; }
        constexpr double to_rad = 2.663161090079238e-07;
        s->cam[0] = x >> 8, s->cam[1] = y >> 8, s->cam[2] = z >> 8, s->dist = dist;
        s->sin_a = std::sin(a * to_rad), s->cos_a = std::cos(a * to_rad);
        s->sin_b = std::sin(b * to_rad), s->cos_b = std::cos(b * to_rad);
        s->sin_c = std::sin(cc * to_rad), s->cos_c = std::cos(cc * to_rad);
        return 0;
    });
    add(r, 0xC00C, "particle tick length", 2, 0, [screen_of](Ctx& c) {
        const auto ms = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Screen* s = screen_of(c, h);
        if (!s) return 0;
        if (ms < 0 || ms > 100) { c.fail(Errc::Range, "bad particle interval"); return 0; }
        s->interval = std::uint32_t(ms), s->wind.interval = std::uint32_t(ms);
        wind_recalc(s->wind);
        return 0;
    });
    add(r, 0xC00D, "particle pre-run", 2, 0, [wp, screen_of](Ctx& c) {
        const auto ms = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Screen* s = screen_of(c, h);
        if (!s || s->interval == 0) return 0;
        const std::int64_t last = s->last_tick;
        std::int64_t saved[2][kPatterns];
        std::memcpy(saved, s->next_spawn, sizeof saved);
        const std::int64_t now = c.vm.now_ms;
        for (std::int32_t i = 1; i <= ms; ++i) update_to(*wp, *s, now + i);
        s->last_tick = last;
        std::memcpy(s->next_spawn, saved, sizeof saved);
        return 0;
    });
    add(r, 0xC00F, "clear particles", 1, 0, [screen_of](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (Screen* s = screen_of(c, h)) {
            for (auto& q : s->slots) q.reset();
            std::memset(s->live_count, 0, sizeof s->live_count), std::memset(s->next_spawn, 0, sizeof s->next_spawn);
        }
        return 0;
    });
    add(r, 0xC010, "particle wind", 12, 0, [wp, screen_of](Ctx& c) {
        std::int32_t a[11];
        for (int i = 10; i >= 0; --i) a[i] = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        Screen* s = screen_of(c, h);
        if (!s) return 0;
        Wind& wd = s->wind;
        wd.base[0] = a[1] >> 8, wd.var[0] = a[2] >> 8, wd.base[1] = a[3] >> 8, wd.var[1] = a[4] >> 8, wd.base[2] = a[5] >> 8, wd.var[2] = a[6] >> 8;
        wd.hold_ms = std::uint32_t(a[7]), wd.hold_var_ms = std::uint32_t(a[8]), wd.move_ms = std::uint32_t(a[9]), wd.move_var_ms = std::uint32_t(a[10]);
        wd.enabled = a[0] != 0 && wind_recalc(wd);
        wind_next(*wp, wd, true);
        return 0;
    });
    for (auto [id, type] : {std::pair{0xC020, 0}, {0xC028, 1}})
        add(r, static_cast<std::uint16_t>(id), type ? "particle group B" : "particle group A", 4, 0, [screen_of, type](Ctx& c) {
            const auto every = static_cast<std::int32_t>(c.pop()), max = static_cast<std::int32_t>(c.pop()), pattern = static_cast<std::int32_t>(c.pop());
            const Word h = c.pop();
            if (!c.ok()) return 0;
            Screen* s = screen_of(c, h);
            if (!s) return 0;
            if (pattern < 0 || pattern >= kPatterns) { c.fail(Errc::Range, "bad pattern number " + std::to_string(pattern)); return 0; }
            std::int32_t sum = 0;
            for (int t = 0; t < 2; ++t)
                for (int p = 0; p < kPatterns; ++p)
                    if (t != type || p != pattern) sum += s->max_count[t][p];
            if (max < 0 || sum + max > kMaxParticles) { c.fail(Errc::Range, "bad object count " + std::to_string(max)); return 0; }
            s->max_count[type][pattern] = max, s->spawn_every[type][pattern] = std::uint32_t(every);
            return 0;
        });
    add(r, 0xC029, "particle pattern B extra", 16, 0, [screen_of](Ctx& c) {
        for (int i = 0; i < 14; ++i) c.pop();
        c.pop();
        const Word h = c.pop();
        if (c.ok()) screen_of(c, h); // stored by the reference engine, not used by its type 1 motion
        return 0;
    });
    // 90-bit patterns: C0 25 (type 0), C0 2D (type 1)
    add(r, 0xC025, "particle pattern A", 11, 0, [wp](Ctx& c) {
        const Word wind_ptr = c.pop();
        std::int32_t v[9];
        for (int i = 8; i >= 0; --i) v[i] = static_cast<std::int32_t>(c.pop());
        const auto pattern = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        const auto wind_weight = static_cast<std::int32_t>(c.read(wind_ptr, 4, false));
        if (!c.ok()) return 0;
        if (pattern < 0 || pattern >= kPatterns) { c.fail(Errc::Range, "bad pattern number " + std::to_string(pattern)); return 0; }
        auto absr = [](std::int32_t x) { return (x < 0 ? -x : x) >> 8; };
        PatternA& d = wp->patterns.a[std::size_t(pattern)];
        d.defined = true, d.x_spread = absr(v[0]), d.y_start = v[1] >> 8, d.z_spread = absr(v[2]);
        d.vx_base = v[3] >> 8, d.vx_var = absr(v[4]), d.vy_base = v[5] >> 8, d.vy_var = absr(v[6]), d.vz_base = v[7] >> 8, d.vz_var = absr(v[8]);
        d.wind = wind_weight >> 8;
        return 0;
    });
    add(r, 0xC02D, "particle pattern B", 18, 0, [wp](Ctx& c) {
        const auto effect = static_cast<std::int32_t>(c.pop()), fade_out = static_cast<std::int32_t>(c.pop()), fade_in = static_cast<std::int32_t>(c.pop());
        const Word wind_ptr = c.pop();
        std::int32_t v[13]; // life base, life var, x spread, y start, z spread, vx b/v, vy b/v, vz b/v, turn b/v
        for (int i = 12; i >= 0; --i) v[i] = static_cast<std::int32_t>(c.pop());
        const auto pattern = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        const auto wind_weight = static_cast<std::int32_t>(c.read(wind_ptr, 4, false));
        if (!c.ok()) return 0;
        if (pattern < 0 || pattern >= kPatterns) { c.fail(Errc::Range, "bad pattern number " + std::to_string(pattern)); return 0; }
        auto absr = [](std::int32_t x) { return (x < 0 ? -x : x) >> 8; };
        PatternB& d = wp->patterns.b[std::size_t(pattern)];
        d.defined = true, d.life_base = v[0], d.life_var = v[1], d.x_spread = absr(v[2]), d.y_start = v[3] >> 8, d.z_spread = absr(v[4]);
        d.vx_base = v[5] >> 8, d.vx_var = absr(v[6]), d.vy_base = v[7] >> 8, d.vy_var = absr(v[8]), d.vz_base = v[9] >> 8, d.vz_var = absr(v[10]);
        d.turn_base = v[11], d.turn_var = v[12], d.fade_in = fade_in ? fade_in : 1, d.fade_out = fade_out ? fade_out : 1, d.effect = effect, d.wind = wind_weight >> 8;
        return 0;
    });
    // C0 24 / C0 2C (pattern, bmp): binds a bitmap (-1 none) to a pattern: 32 scaled copies
    for (auto [id, type] : {std::pair{0xC024, 0}, {0xC02C, 1}})
        add(r, static_cast<std::uint16_t>(id), "bind particle bitmap", 2, 0, [wp, type](Ctx& c) {
            const auto bmp = static_cast<std::int32_t>(c.pop()), pattern = static_cast<std::int32_t>(c.pop());
            if (!c.ok()) return 0;
            if (pattern < 0 || pattern >= kPatterns) { c.fail(Errc::Range, "bad pattern number " + std::to_string(pattern)); return 0; }
            auto& pic = wp->patterns.picture[type][std::size_t(pattern)];
            if (bmp == -1) { for (auto& v : pic) free_slots(*wp, v); return 0; }
            if (!make_scales(*wp, static_cast<Word>(bmp), pic, &wp->patterns.size[type][std::size_t(pattern)])) c.fail(Errc::Range, "bitmap " + std::to_string(bmp) + " cannot be used for particles");
            return 0;
        });
    // C0 1F (mode): how C0 18 derives the frame rates
    add(r, 0xC01F, "particle animation mode", 1, 0, [wp](Ctx& c) {
        const Word mode = c.pop();
        if (!c.ok()) return 0;
        if (mode > 2) { c.fail(Errc::Range, "bad control mode " + std::to_string(mode)); return 0; }
        wp->patterns.anim_mode = int(mode);
        return 0;
    });
    // C0 18 (type, pattern, count, firstBmp, a, b, frames) and C0 1A (+ level): frames firstBmp .. firstBmp + count - 1 become the
    // animation of a pattern (C0 1A: the alternate image of a type 1 pattern)
    for (auto [id, alt] : {std::pair{0xC018, false}, {0xC01A, true}}) {
        const int pops = alt ? 8 : 7;
        add(r, static_cast<std::uint16_t>(id), alt ? "particle alternate animation" : "particle animation", pops, 0, [wp, alt](Ctx& c) {
            Word level = 0;
            if (alt) level = c.pop();
            const auto frames = static_cast<std::int32_t>(c.pop()), b = static_cast<std::int32_t>(c.pop()), a = static_cast<std::int32_t>(c.pop());
            const auto first = static_cast<std::int32_t>(c.pop()), count = static_cast<std::int32_t>(c.pop());
            const auto pattern = static_cast<std::int32_t>(c.pop()), type = static_cast<std::int32_t>(c.pop());
            if (!c.ok()) return 0;
            if (alt && level > 0x100) { c.fail(Errc::Range, "bad mix ratio"); return 0; }
            if (type < 0 || type > (alt ? 1 : 1) || (alt && type != 1)) { c.fail(Errc::Range, "bad particle type " + std::to_string(type)); return 0; }
            if (pattern < 0 || pattern >= kPatterns) { c.fail(Errc::Range, "bad pattern number " + std::to_string(pattern)); return 0; }
            if (count > 32) { c.fail(Errc::Range, "bad bitmap count " + std::to_string(count)); return 0; }
            const std::uint32_t f16 = static_cast<std::uint32_t>(frames) << 16;
            std::uint32_t rate1 = 0, rate2 = 0;
            switch (wp->patterns.anim_mode) {
            case 0: rate1 = a ? f16 / std::uint32_t(a) : 0, rate2 = b ? f16 / std::uint32_t(b) : 0; if (!a || !b) rate2 = 0; break;
            case 1: rate1 = a ? f16 / std::uint32_t(a) : 0, rate2 = (a && b) ? f16 / std::uint32_t(a + b) - rate1 : 0; break;
            default: rate1 = a ? f16 / std::uint32_t(a) : 0, rate2 = a ? std::uint32_t((std::uint64_t{rate1} * std::uint32_t(b)) / std::uint32_t(a)) : 0; break;
            }
            Animation& an = alt ? wp->patterns.alt[std::size_t(pattern)] : wp->patterns.anim[type][std::size_t(pattern)];
            for (auto& v : an.frames) free_slots(*wp, v);
            an = Animation{};
            if (alt) wp->patterns.alt_level[std::size_t(pattern)] = std::int32_t(level);
            if (count <= 0) return 0;
            if (first == -1) { c.fail(Errc::Range, "bitmap " + std::to_string(first) + " does not exist"); return 0; }
            an.count = count, an.rate1 = std::int32_t(rate1), an.rate2 = std::int32_t(rate2);
            for (std::int32_t i = 0; i < count; ++i) {
                std::array<std::vector<Word>, kScales> one;
                if (!make_scales(*wp, static_cast<Word>(first + i), one, nullptr)) { c.fail(Errc::Range, "bitmap " + std::to_string(first + i) + " does not exist"); an = Animation{}; return 0; }
                for (int k = 0; k < kScales; ++k) an.frames[std::size_t(k)].push_back(one[std::size_t(k)].empty() ? kNoImage : one[std::size_t(k)][0]);
            }
            return 0;
        });
    }
    add(r, 0xC01B, "particle alternate level", 3, 0, [wp](Ctx& c) {
        const Word level = c.pop();
        const auto pattern = static_cast<std::int32_t>(c.pop()), type = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (level > 0x100) { c.fail(Errc::Range, "bad mix ratio"); return 0; }
        if (type != 1) { c.fail(Errc::Range, "bad particle type"); return 0; }
        if (pattern < 0 || pattern >= kPatterns) { c.fail(Errc::Range, "bad pattern number"); return 0; }
        if (wp->patterns.alt[std::size_t(pattern)].count <= 0) { c.fail(Errc::Range, "the pattern has no alternate image"); return 0; }
        wp->patterns.alt_level[std::size_t(pattern)] = std::int32_t(level);
        return 0;
    });

    // ---- rain screens ----
    auto rworld = std::make_shared<RainWorld>();
    rworld->display = display;
    RainWorld* rp = rworld.get();
    state.per_frame.push_back([rworld, rp](std::int64_t now) {
        for (int i = 0; i < kRains; ++i) {
            Rain& r = rp->rains[std::size_t(i)];
            if (!r.used) continue;
            rain_update(*rp, r, now);
            Sprite* sp = rp->display->find_sprite(kRainTag | Word(i));
            if (sp) sp->visible = r.visible && rp->effects_enabled;
        }
        if (!rp->effects_enabled || rp->next_frame > now) return;
        for (int i = 0; i < kRains; ++i) {
            Rain& r = rp->rains[std::size_t(i)];
            if (r.used && r.started && r.visible) rain_redraw(r, *rp->display, kRainSurface + Word(i));
        }
        rp->next_frame += ((now - rp->next_frame) / rp->frame_ms + 1) * rp->frame_ms;
    });
    auto rain_of = [rp](Ctx& c, Word h) -> Rain* {
        const bool ok = (h & 0xFFFFFFF8u) == kRainTag && rp->rains[h & 7].used;
        if (!ok) c.fail(Errc::Range, "no rain screen " + std::to_string(h));
        return ok ? &rp->rains[h & 7] : nullptr;
    };
    auto rain_sprite = [rp](Word h, Rain& r) {
        Sprite& sp = rp->display->sprite(h);
        sp.slot = kRainSurface + (h & 7), sp.x = r.x, sp.y = r.y, sp.blend = blend_mode_supported(std::uint32_t(r.effect)) ? std::uint32_t(r.effect) : 1;
        sp.alpha = std::uint32_t(r.level), sp.priority = r.prio, sp.visible = r.visible && rp->effects_enabled;
    };
    add(r, 0xC040, "create rain screen", 2, 1, [rp, rain_sprite](Ctx& c) {
        const Word h = c.pop(), w = c.pop();
        if (!c.ok()) return 0;
        for (Word i = 0; i < kRains; ++i) {
            if (rp->rains[i].used) continue;
            if (w == 0 || h == 0 || w > 0x2000 || h > 0x2000) { c.push(0); return 0; } // the original raises no error for a bad size
            rp->rains[i] = Rain{};
            Rain& r = rp->rains[i];
            r.used = true, r.w = w, r.h = h;
            rp->display->create_slot(kRainSurface + i, w, h, SlotFormat::Bgra);
            rp->display->fill_slot(kRainSurface + i, 0);
            rain_sprite(kRainTag | i, r);
            c.push(kRainTag | i);
            return 0;
        }
        c.fail(Errc::Range, "no more rain screens");
        return 0;
    });
    add(r, 0xC041, "delete rain screen", 1, 0, [rp, rain_of](Ctx& c) {
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (Rain* r2 = rain_of(c, h)) {
            rp->display->free_slot(kRainSurface + (h & 7));
            rp->display->remove_sprite(h);
            *r2 = Rain{};
        }
        return 0;
    });
    add(r, 0xC042, "start rain", 2, 0, [rain_of](Ctx& c) {
        const Word preroll = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (Rain* r2 = rain_of(c, h)) { r2->started = true, r2->drops.clear(), r2->last_time = c.vm.now_ms - std::int64_t(preroll); }
        return 0;
    });
    add(r, 0xC043, "rain mask", 2, 0, [rain_of](Ctx& c) {
        const auto bmp = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (Rain* r2 = rain_of(c, h)) r2->mask = bmp; // stored; the mask is not applied
        return 0;
    });
    add(r, 0xC044, "show rain screen", 2, 0, [rp, rain_of, rain_sprite](Ctx& c) {
        const Word on = c.pop(), h = c.pop();
        if (!c.ok()) return 0;
        if (Rain* r2 = rain_of(c, h)) { r2->visible = on != 0; rain_sprite(h, *r2); }
        return 0;
    });
    add(r, 0xC045, "place rain screen", 6, 0, [rain_of, rain_sprite](Ctx& c) {
        const Word prio = c.pop(), level = c.pop(), effect = c.pop();
        const auto y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (level > 0x100 || prio > 0xFFFF) { c.fail(Errc::Range, "rain screen parameter out of range"); return 0; }
        if (Rain* r2 = rain_of(c, h)) { r2->x = x, r2->y = y, r2->effect = std::int32_t(effect), r2->level = std::int32_t(level), r2->prio = prio; rain_sprite(h, *r2); }
        return 0;
    });
    add(r, 0xC046, "rain area", 7, 0, [rain_of](Ctx& c) {
        const auto z_max = static_cast<std::int32_t>(c.pop()), y_bottom = static_cast<std::int32_t>(c.pop()), x_max = static_cast<std::int32_t>(c.pop());
        const auto z_min = static_cast<std::int32_t>(c.pop()), y_top = static_cast<std::int32_t>(c.pop()), x_min = static_cast<std::int32_t>(c.pop());
        const Word h = c.pop();
        if (!c.ok()) return 0;
        if (Rain* r2 = rain_of(c, h)) r2->x_min = x_min, r2->y_top = y_top, r2->z_min = z_min, r2->x_max = x_max, r2->y_bottom = y_bottom, r2->z_max = z_max;
        return 0;
    });
    // C0 47 / 48 / 4B / 4E: a value that must not be 0
    for (auto [id, which] : {std::pair{0xC047, 0}, {0xC048, 1}, {0xC04B, 2}, {0xC04E, 3}})
        add(r, static_cast<std::uint16_t>(id), "rain value", 2, 0, [rain_of, which](Ctx& c) {
            const auto n = static_cast<std::int32_t>(c.pop());
            const Word h = c.pop();
            if (!c.ok()) return 0;
            Rain* r2 = rain_of(c, h);
            if (!r2) return 0;
            if (n == 0) { c.fail(Errc::Range, "bad rain value 0"); return 0; }
            if (which == 0) r2->fall = n << 8;
            else if (which == 1) r2->length = n << 8;
            else if (which == 2) r2->stage_ms = std::uint32_t(n);
            else r2->dist = n;
            return 0;
        });
    add(r, 0xC049, "rain colour", 2, 0, [rain_of](Ctx& c) { const Word argb = c.pop(), h = c.pop(); if (c.ok()) if (Rain* r2 = rain_of(c, h)) r2->colour = argb; return 0; });
    add(r, 0xC04A, "rain drops per stage", 2, 0, [rain_of](Ctx& c) { const auto n = static_cast<std::int32_t>(c.pop()); const Word h = c.pop(); if (c.ok()) if (Rain* r2 = rain_of(c, h)) r2->per_stage = std::uint32_t(n); return 0; });
    for (auto [id, angles] : {std::pair{0xC04C, false}, {0xC04D, true}})
        add(r, static_cast<std::uint16_t>(id), angles ? "rain angles" : "rain camera", 4, 0, [rain_of, angles](Ctx& c) {
            const auto z = static_cast<std::int32_t>(c.pop()), y = static_cast<std::int32_t>(c.pop()), x = static_cast<std::int32_t>(c.pop());
            const Word h = c.pop();
            if (!c.ok()) return 0;
            if (Rain* r2 = rain_of(c, h)) { std::int32_t* dst = angles ? r2->ang : r2->cam; dst[0] = x, dst[1] = y, dst[2] = z; }
            return 0;
        });
    // C0 4F (enable, fps): the effects switch and their frame rate
    add(r, 0xC04F, "effect frame rate", 2, 0, [rp](Ctx& c) {
        const Word fps = c.pop(), enable = c.pop();
        if (!c.ok()) return 0;
        if (fps >= 1 && fps <= 1000) { rp->frame_ms = 1000 / fps, rp->next_frame = 0, rp->effects_enabled = enable != 0; }
        return 0;
    });
}

} // namespace obgi
