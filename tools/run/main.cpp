// obgi_run: boots the game scripts headless and runs them until something stops them.
//   obgi_run <game dir> [--frames N] [--driver d] [--entry arc member]
// Headless time is virtual (16 ms per pass); windowed time follows the monotonic host clock. The first script error, usually a
// sub-opcode that is not implemented yet, is printed with the frame it happened in.
#include <cstdio>
#include <chrono>
#include <thread>
#include "runtime/session.hpp"
#include <cstring>
#include <iomanip>
#include <deque>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>
#include <filesystem>
#include <charconv>
#include <string>

#include "bindings/arc_program_provider.hpp"
#include "bindings/system_subops.hpp"
#include "render/display.hpp"
#include "vm/vm.hpp"

namespace {
int die(const std::string& msg) {
    std::fprintf(stderr, "obgi_run: %s\n", msg.c_str());
    return 1;
}

// Minimal PNG writer (stored deflate blocks) for --shot.
void write_png(const std::string& path, const std::vector<std::uint32_t>& px, unsigned w, unsigned h) {
    auto crc = [](const std::vector<unsigned char>& d, std::size_t from) {
        static std::uint32_t table[256];
        if (!table[1])
            for (std::uint32_t n = 0; n < 256; ++n) {
                std::uint32_t c = n;
                for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                table[n] = c;
            }
        std::uint32_t c = 0xFFFFFFFFu;
        for (std::size_t i = from; i < d.size(); ++i) c = table[(c ^ d[i]) & 255] ^ (c >> 8);
        return c ^ 0xFFFFFFFFu;
    };
    std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    auto be32 = [](std::vector<unsigned char>& v, std::uint32_t x) { for (int i = 3; i >= 0; --i) v.push_back(static_cast<unsigned char>(x >> (8 * i))); };
    auto chunk = [&](const char* type, const std::vector<unsigned char>& data) {
        be32(out, static_cast<std::uint32_t>(data.size()));
        const std::size_t at = out.size();
        out.insert(out.end(), type, type + 4);
        out.insert(out.end(), data.begin(), data.end());
        be32(out, crc(out, at));
    };
    std::vector<unsigned char> ihdr;
    be32(ihdr, w), be32(ihdr, h);
    ihdr.insert(ihdr.end(), {8, 2, 0, 0, 0});
    chunk("IHDR", ihdr);
    std::vector<unsigned char> raw;
    for (unsigned y = 0; y < h; ++y) {
        raw.push_back(0);
        for (unsigned x = 0; x < w; ++x) {
            const std::uint32_t p = px[std::size_t{y} * w + x];
            raw.push_back(static_cast<unsigned char>(p >> 16)), raw.push_back(static_cast<unsigned char>(p >> 8)), raw.push_back(static_cast<unsigned char>(p));
        }
    }
    std::vector<unsigned char> z = {0x78, 0x01};
    std::uint32_t a = 1, b = 0;
    for (unsigned char c : raw) a = (a + c) % 65521, b = (b + a) % 65521;
    for (std::size_t at = 0; at < raw.size(); at += 65535) {
        const std::size_t n = std::min<std::size_t>(65535, raw.size() - at);
        z.push_back(at + n >= raw.size());
        z.push_back(n & 255), z.push_back(n >> 8), z.push_back(~n & 255), z.push_back((~n >> 8) & 255);
        z.insert(z.end(), raw.begin() + at, raw.begin() + at + n);
    }
    be32(z, (b << 16) | a);
    chunk("IDAT", z);
    chunk("IEND", {});
    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
}
} // namespace

int main(int argc, char** argv) {
    using namespace obgi;
    if (std::getenv("OBGI_UNBUF")) std::setvbuf(stdout, nullptr, _IONBF, 0); // keep the log of a crashing run
    std::string game, driver_s, entry_arc = "autoload.arc", entry_name = "ipl._bp";
    long frames = 20000;
    std::string effects_path, shot_path, mouse_script;
    long shot_frame = -1;
    bool window = false, frames_set = false, show_threads = false, live_cli = false;
    long progress_every = 0, click_first = 0, click_period = 0;
    auto number = [](const char* text, long& value) {
        const char* end = text + std::strlen(text);
        auto result = std::from_chars(text, end, value);
        return result.ec == std::errc{} && result.ptr == end && value >= 0;
    };
    std::string survey, data_dir, dump_arc, dump_name, dump_out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") {
            std::printf("Usage: obgi_run <game dir> [options]\n"
                        "  --frames N             stop after N frames (default 20000)\n"
                        "  --window               use a window and real time\n"
                        "  --data-dir DIR         isolate saved data\n"
                        "  --driver NAME          select GPU driver\n"
                        "  --entry ARC MEMBER     select startup script\n"
                        "  --shot FRAME FILE      save a screenshot\n"
                        "  --click-every FIRST PERIOD  click periodically (zero-based frames)\n"
                        "  --mouse SCRIPT         pointer moves: frame:x:y,...\n"
                        "  --progress-every N     report every N frames\n"
                        "  --trace                print sub-opcodes as they execute\n"
                        "  --threads              report live threads at frame limit\n"
                        "  --effects FILE         write registered stack effects\n"
                        "  --survey FILE          enable diagnostic sub-opcode stubs\n"
                        "  --dump ARC MEMBER FILE extract a program block\n");
            return 0;
        }
        if (a == "--frames" && i + 1 < argc) {
            if (!number(argv[++i], frames)) return die("--frames requires a non-negative integer");
            frames_set = true;
        }
        else if (a == "--progress-every" && i + 1 < argc) {
            if (!number(argv[++i], progress_every) || !progress_every) return die("--progress-every requires a positive integer");
        }
        else if (a == "--click-every" && i + 2 < argc) {
            if (!number(argv[++i], click_first) || !number(argv[++i], click_period) || !click_period)
                return die("--click-every requires a non-negative first frame and positive period");
        }
        else if (a == "--trace") live_cli = true;
        else if (a == "--threads") show_threads = true;
        else if (a == "--mouse" && i + 1 < argc) mouse_script = argv[++i];
        else if (a == "--data-dir" && i + 1 < argc) data_dir = argv[++i];
        else if (a == "--dump" && i + 3 < argc) { // --dump <arc> <member> <out file>: a program block, DSC removed
            dump_arc = argv[i + 1], dump_name = argv[i + 2], dump_out = argv[i + 3];
            i += 3;
        }
        else if (a == "--shot" && i + 2 < argc) shot_frame = std::atol(argv[i + 1]), shot_path = argv[i + 2], i += 2;
        else if (a == "--window") window = true;
        else if (a == "--effects" && i + 1 < argc) effects_path = argv[++i];
        else if (a == "--survey" && i + 1 < argc) survey = argv[++i];
        else if (a == "--driver" && i + 1 < argc) driver_s = argv[++i];
        else if (a == "--entry" && i + 2 < argc) entry_arc = argv[++i], entry_name = argv[++i];
        else if (!a.empty() && a[0] == '-') return die("unknown or incomplete option: " + a);
        else if (game.empty()) game = a;
        else return die("unexpected argument: " + a);
    }
    if (window && !frames_set) frames = 0x7FFFFFFF;
    if (game.empty()) return die("usage: obgi_run <game dir> [--frames N] [--driver d] [--entry arc member]");

    if (!dump_out.empty()) {
        ArcProgramProvider dumper(std::filesystem::path(std::u8string(game.begin(), game.end())));
        auto bytes = dumper.load(dump_arc, dump_name);
        if (!bytes) return die(bytes.error().message);
        std::ofstream out(dump_out, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes->data()), std::streamsize(bytes->size()));
        std::printf("wrote %zu bytes\n", bytes->size());
        return 0;
    }
    SessionOptions options;
    options.game_dir = std::filesystem::path(std::u8string(game.begin(), game.end()));
    if (!data_dir.empty()) options.data_dir = std::filesystem::path(data_dir);
    options.window = window;
    options.deterministic = !window;
    options.driver = driver_s;
    auto created = EngineSession::create(std::move(options));
    if (!created) return die(created.error().message);
    auto session = std::move(*created);
    auto& vm = session->machine();
    auto& registry = session->registry();
    auto& system = session->system();
    auto* display = &session->display();
    if (!effects_path.empty()) { // id, pops, pushes of every registered sub-opcode (for comparing with the real game)
        std::ofstream ef(effects_path);
        for (unsigned id = 0x8000; id <= 0xFFFF; ++id)
            if (const auto* d = registry.find(static_cast<std::uint16_t>(id)))
                ef << std::hex << std::uppercase << std::setfill('0') << std::setw(2) << (id >> 8) << ':' << std::setw(2) << (id & 255) << std::dec << '\t' << int(d->pops) << '\t' << int(d->pushes) << '\n';
    }
    std::printf("%zu sub-opcodes registered\n", registry.size());
    if (const char* dump = std::getenv("OBGI_DUMPOPS")) { // path: one "XXXX name pops pushes" line per registered sub-opcode
        std::ofstream out(dump);
        for (const auto& [id, def] : registry.all()) out << std::hex << std::uppercase << id << ' ' << def.name << ' ' << std::dec << int(def.pops) << ' ' << int(def.pushes) << '\n';
    }

    // --survey <subop_effects.tsv>: diagnostic only. Every sub-opcode that is not implemented but has a measured
    // stack effect becomes a stub that pops, pushes zeros and returns Continue; the hits are listed at the end.
    std::vector<std::pair<std::uint16_t, std::string>> hit_order;
    std::map<std::uint16_t, long> hits;
    if (!survey.empty()) {
        std::ifstream in(survey);
        std::string line;
        while (std::getline(in, line)) {
            std::vector<std::string> col;
            std::stringstream ss(line);
            for (std::string c; std::getline(ss, c, '\t');) col.push_back(c);
            if (col.size() < 5 || col[0].size() != 5 || col[0][2] != ':') continue;
            const auto is_num = [](const std::string& s) { return !s.empty() && s.find_first_not_of("0123456789") == std::string::npos; };
            if (!is_num(col[3]) || !is_num(col[4])) continue;
            const auto id = static_cast<std::uint16_t>(std::stoul(col[0].substr(0, 2) + col[0].substr(3, 2), nullptr, 16));
            if (registry.find(id)) continue;
            const int pops = std::stoi(col[3]), pushes = std::stoi(col[4]);
            registry.add({id, "stub " + col[0], static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes),
                          [id, pops, pushes, &hits, &hit_order, name = col[0]](vm::Ctx& c) {
                              for (int i = 0; i < pops; ++i) c.pop();
                              for (int i = 0; i < pushes; ++i) c.push(0);
                              if (hits[id]++ == 0) hit_order.emplace_back(id, name + " pops " + std::to_string(pops) + " pushes " + std::to_string(pushes));
                              return 0;
                          }});
        }
        std::printf("%zu sub-opcodes registered (with survey stubs)\n", registry.size());
    }
    auto report_hits = [&] {
        for (const auto& [id, text] : hit_order) std::printf("  stub hit x%ld: %s\n", hits[id], text.c_str());
    };

    if (auto boot = session->boot(entry_arc, entry_name); !boot) return die(boot.error().message);

    // the last sub-opcodes run, printed when something stops the scripts
    std::deque<std::string> recent;
    const bool live = live_cli || std::getenv("OBGI_LIVE") != nullptr; // print every sub-opcode as it runs (stderr)
    vm.trace = [&recent, &vm, live](const vm::Thread& t, std::uint16_t id, const std::vector<vm::Word>& out) {
        static long event_no = 0;
        if (const char* da = std::getenv("OBGI_DUMPMEM_AT")) { // "event,offset,length,path": global memory before that sub-opcode ends
            unsigned long ev = 0, off = 0, len = 0;
            char path[512] = {};
            if (std::sscanf(da, "%lu,%lx,%lx,%511s", &ev, &off, &len, path) == 4 && static_cast<long>(ev) == event_no) {
                std::vector<std::uint8_t> mem(len);
                if (vm.global.read(static_cast<vm::Word>(off), len, mem.data()))
                    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(mem.data()), std::streamsize(len));
            }
        }
        if (const char* dp = std::getenv("OBGI_DUMPPROG_AT")) { // "event,path": the thread's program area at that sub-opcode
            unsigned long ev = 0;
            char path[512] = {};
            if (std::sscanf(dp, "%lu,%511s", &ev, path) == 2 && static_cast<long>(ev) == event_no)
                std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(t.program.data()), std::streamsize(t.program.size()));
        }
        if (const char* sa = std::getenv("OBGI_STACK_AT")) { // "first,last": the top 12 expression stack values after those sub-opcodes
            unsigned long a = 0, b = 0;
            if (std::sscanf(sa, "%lu,%lu", &a, &b) == 2 && static_cast<unsigned long>(event_no) >= a && static_cast<unsigned long>(event_no) <= b) {
                std::fprintf(stderr, "stack#%ld %s:", event_no, vm::subop_name(id).c_str());
                for (unsigned i = 1; i <= 12; ++i)
                    std::fprintf(stderr, " %X", t.expr[(t.depth + t.expr.size() - i) % t.expr.size()]);
                std::fprintf(stderr, "\n");
            }
        }
        ++event_no;
        char buf[160];
        int n = std::snprintf(buf, sizeof buf, "t%u ip=%X %s ->", t.id, t.ip, vm::subop_name(id).c_str());
        for (vm::Word w : out) n += std::snprintf(buf + n, sizeof buf - n, " %X", w);
        if (live) std::fprintf(stderr, "%s\n", buf);
        recent.emplace_back(buf);
        if (recent.size() > 25) recent.pop_front();
    };
    std::deque<std::string> recent_ops;
    if (std::getenv("OBGI_ITRACE")) {
        // OBGI_IP_WATCH=<hex ip>: the first time an instruction at that ip runs, the last 80 instructions are printed
        const char* watch_env = std::getenv("OBGI_IP_WATCH");
        const unsigned watch = watch_env ? static_cast<unsigned>(std::strtoul(watch_env, nullptr, 16)) : 0;
        vm.trace_op = [&recent_ops, watch, done = false](const vm::Thread& t, std::uint8_t op) mutable {
            if (watch && !done && t.ip + 3 >= watch && t.ip <= watch + 3) {
                done = true;
                for (const auto& line : recent_ops) std::fprintf(stderr, "  %s\n", line.c_str());
            }
            char buf[200];
            int n = std::snprintf(buf, sizeof buf, "ip=%X op=%02X sp=%X depth=%u top:", t.ip, op, t.sp, t.depth);
            for (unsigned i = 0; i < 4 && i < t.depth; ++i) n += std::snprintf(buf + n, sizeof buf - n, " %X", t.expr[t.depth - 1 - i]);
            recent_ops.emplace_back(buf);
            if (recent_ops.size() > 80) recent_ops.pop_front();
        };
    }
    const bool progress = std::getenv("OBGI_PROGRESS") != nullptr; // one line per frame on stderr, for finding a stuck frame
    const auto started = std::chrono::steady_clock::now();
    for (long f = 0; f < frames; ++f) {
        if (progress || (progress_every && f % progress_every == 0)) {
            std::fprintf(stderr, "frame %ld\n", f);
            std::fflush(stderr);
        }
        if (!session->pump_events()) {
            std::printf("window closed after %ld frames\n", f);
            report_hits();
            return 0;
        }
        if (const char* wh = std::getenv("OBGI_WHEEL")) // "frame:notches,...": scripted wheel turns (negative = towards the user)
            for (std::stringstream ss(wh); ss.good();) {
                std::string tok;
                std::getline(ss, tok, ',');
                long at = 0;
                int n = 0;
                if (std::sscanf(tok.c_str(), "%ld:%d", &at, &n) == 2 && at == f) {
                    for (std::int64_t count = n < 0 ? -std::int64_t(n) : std::int64_t(n); count > 0; --count) {
                        system.wheel_events.push_back(n > 0 ? 1 : -1);
                        ++system.input_serial;
                    }
                }
            }
        if (const char* hold = std::getenv("OBGI_HOLD")) // "frame:1|0,...": scripted left button held (1) or released (0) for headless drags
            for (std::stringstream ss(hold); ss.good();) {
                std::string tok;
                std::getline(ss, tok, ',');
                long at = 0;
                int down = 0;
                if (std::sscanf(tok.c_str(), "%ld:%d", &at, &down) == 2 && at <= f) system.left_down = down != 0;
            }
        if (const char* pr = std::getenv("OBGI_PRESS")) // "frame:vk,frame:vk": scripted key presses for headless runs (vk 1 = left mouse button)
            for (std::stringstream ss(pr); ss.good();) {
                std::string tok;
                std::getline(ss, tok, ',');
                long at = 0;
                unsigned vk = 0;
                if (std::sscanf(tok.c_str(), "%ld:%x", &at, &vk) == 2 && at == f) system.press_input(vk);
            }
        if (click_period && f >= click_first && (f - click_first) % click_period == 0) system.press_input(1);
        if (const char* ce = std::getenv("OBGI_CLICK_EVERY")) { // "first:period": a left click every `period` frames from `first` on
            long first = 0, period = 0;
            if (std::sscanf(ce, "%ld:%ld", &first, &period) == 2 && period > 0 && f >= first && (f - first) % period == 0) system.press_input(1);
        }
        if (const char* pm = mouse_script.empty() ? std::getenv("OBGI_MOUSE") : mouse_script.c_str()) // "frame:x:y,...": scripted pointer moves for headless runs
            for (std::stringstream ss(pm); ss.good();) {
                std::string tok;
                std::getline(ss, tok, ',');
                long at = 0;
                int mx = 0, my = 0;
                if (std::sscanf(tok.c_str(), "%ld:%d:%d", &at, &mx, &my) == 3 && at <= f) system.mouse_x = mx, system.mouse_y = my;
            }
        const auto now_ms = window
            ? std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count()
            : f * std::int64_t{16};
        auto st = session->tick(now_ms);
        if (st) if (auto shown = session->present(); !shown) return die(shown.error().message);
        if (const char* dm = std::getenv("OBGI_DUMPMEM")) { // "offset,length,frame,path": script global memory after that frame
            unsigned long off = 0, len = 0, fr = 0;
            char path[512] = {};
            if (std::sscanf(dm, "%lx,%lx,%lu,%511s", &off, &len, &fr, path) == 4 && static_cast<long>(fr) == f) {
                std::vector<std::uint8_t> buf(len);
                if (vm.global.read(static_cast<vm::Word>(off), len, buf.data()))
                    std::ofstream(path, std::ios::binary).write(reinterpret_cast<const char*>(buf.data()), std::streamsize(len));
            }
        }
        if (f == shot_frame) {
            auto capture = session->capture();
            if (!capture) std::fprintf(stderr, "shot: %s\n", capture.error().message.c_str());
            else write_png(shot_path, *capture, display->screen_width(), display->screen_height());
            if (const char* l = std::getenv("OBGI_SHOTSLOTS")) // slot ids to save next to the shot, e.g. 1263,1151
                for (std::stringstream ss(l); ss.good();) {
                    std::string tok;
                    std::getline(ss, tok, ',');
                    if (tok.empty()) continue;
                    const auto id = static_cast<std::uint32_t>(std::stoul(tok));
                    const SlotInfo* info = display->slot(id);
                    if (!info) { std::fprintf(stderr, "slot %u missing\n", id); continue; }
                    if (auto px = display->read_slot(id); px) write_png(shot_path + "." + tok + ".png", *px, info->width, info->height);
                }
        }
        if (!st) {
            std::printf("frame %ld: %s\n", f, st.error().message.c_str());
            for (const auto& th_ : vm.threads())
                if (!th_->dead) std::printf("  thread %u ip=0x%X modules=%zu\n", th_->id, th_->ip, th_->modules.size());
            report_hits();
            for (const auto& th_ : vm.threads())
                if (!th_->dead)
                    for (const auto& cl : th_->calls) std::printf("  call from 0x%X (returns to 0x%X)\n", cl.origin, cl.ret);
            for (const auto& th_ : vm.threads())
                if (!th_->dead)
                    for (const auto& m : th_->modules) std::printf("  module %s/%s at 0x%X size 0x%X\n", m.arc.c_str(), m.name.c_str(), m.start, m.size);
            std::printf("last sub-opcodes:\n");
            for (const auto& line : recent) std::printf("  %s\n", line.c_str());
            for (const auto& line : recent_ops) std::printf("  %s\n", line.c_str());
            return 2;
        }
        if (system.quit_requested) {
            std::printf("the script asked to quit at frame %ld\n", f + 1);
            report_hits();
            return 0;
        }
        if (*st == vm::FrameStatus::AllThreadsDead) {
            std::printf("all threads finished after %ld frames\n", f + 1);
            report_hits();
            return 0;
        }
        if (window) std::this_thread::sleep_until(started + std::chrono::milliseconds(vm.now_ms + std::max(system.frame_ms, 1u)));
    }
    std::printf("ran %ld frames without stopping\n", frames);
    report_hits();
    if (const char* dp = std::getenv("OBGI_DUMPPROG_END")) // "id,path": that thread's program area after the last frame
        if (const char* comma = std::strchr(dp, ','))
            if (const vm::Thread* th = vm.find(static_cast<vm::Word>(std::strtoul(dp, nullptr, 10))))
                std::ofstream(comma + 1, std::ios::binary).write(reinterpret_cast<const char*>(th->program.data()), std::streamsize(th->program.size()));
    if (show_threads || std::getenv("OBGI_THREADS")) // where every live thread is parked at the end
        for (const auto& th_ : vm.threads())
            if (!th_->dead)
                std::printf("  thread %u ip=0x%X wait=%d until=%lld now=%lld queue=%zu modules=%zu\n", th_->id, th_->ip, int(th_->wait.kind),
                            static_cast<long long>(th_->wait.until), static_cast<long long>(vm.now_ms), th_->queue.size(), th_->modules.size());
    return 0;
}
