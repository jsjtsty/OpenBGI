#include "bindings/system_subops.hpp"
#include "bindings/panel.hpp"

#include <algorithm>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

#include "formats/sdc.hpp"
#include "resources/directory_index.hpp"
#include "text/encoding.hpp"

namespace obgi {

namespace {

using vm::Ctx;
using vm::Word;

void add(vm::SubopRegistry& r, std::uint16_t id, const char* name, int pops, int pushes, vm::SubopFn fn) {
    r.add({id, name, static_cast<std::uint8_t>(pops), static_cast<std::uint8_t>(pushes), std::move(fn)});
}

// pop one value and keep it under the op's id; `limit` (inclusive) makes a larger value a script error
void add_setter(vm::SubopRegistry& r, SystemState& s, std::uint16_t id, const char* name, Word limit = 0xFFFFFFFFu) {
    SystemState* st = &s;
    add(r, id, name, 1, 0, [st, id, limit, name](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        if (v > limit) {
            c.fail(Errc::Range, std::string(name) + ": value " + std::to_string(v) + " out of range");
            return 0;
        }
        st->values[id] = v;
        return 0;
    });
}

// pop one value; below 2 it is stored and 1 is pushed, otherwise nothing changes and 0 is pushed
void add_flag(vm::SubopRegistry& r, SystemState& s, std::uint16_t id, const char* name) {
    SystemState* st = &s;
    add(r, id, name, 1, 1, [st, id](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        if (v < 2) st->values[id] = v;
        c.push(v < 2 ? 1 : 0);
        return 0;
    });
}

// A script path (CP932, Windows separators) as a host path, redirected from the game directory into the data directory.
std::filesystem::path host_path(const SystemState& s, const std::string& script_path) {
    // The host game path is inserted into script strings as UTF-8. Decode only
    // the script-provided suffix; decoding the host prefix as CP932 corrupts it.
    const std::string prefix = s.game_dir.string() + "\\";
    const bool in_game = script_path.starts_with(prefix);
    std::u32string wide = cp932_to_utf32(in_game ? script_path.substr(prefix.size()) : script_path);
    std::string utf8;
    for (char32_t ch : wide) {
        if (ch == U'\\') ch = U'/';
        if (ch < 0x80) utf8 += char(ch);
        else if (ch < 0x800) utf8 += {char(0xC0 | (ch >> 6)), char(0x80 | (ch & 0x3F))};
        else if (ch < 0x10000) utf8 += {char(0xE0 | (ch >> 12)), char(0x80 | ((ch >> 6) & 0x3F)), char(0x80 | (ch & 0x3F))};
        else utf8 += {char(0xF0 | (ch >> 18)), char(0x80 | ((ch >> 12) & 0x3F)), char(0x80 | ((ch >> 6) & 0x3F)), char(0x80 | (ch & 0x3F))};
    }
    std::filesystem::path p(std::u8string(utf8.begin(), utf8.end()));
    if (in_game) {
        // The prefix came from the host itself. A simple relative suffix can be
        // redirected without weakly_canonical's filesystem lookups.
        auto normalized = p.lexically_normal();
        if (!normalized.is_absolute() && (normalized.empty() || *normalized.begin() != ".."))
            return s.data_dir / normalized;
        p = s.game_dir / p;
    }
    std::error_code ec;
    auto rel = std::filesystem::relative(p, s.game_dir, ec);
    if (!ec && !rel.empty() && *rel.begin() != "..") return s.data_dir / rel;
    return p;
}

// case-insensitive "*" / "?" wildcard match of a file name
bool wildcard_match(const std::string& pattern, const std::string& name) {
    auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c; };
    std::size_t p = 0, n = 0, star = std::string::npos, mark = 0;
    while (n < name.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || lower(pattern[p]) == lower(name[n]))) { ++p; ++n; }
        else if (p < pattern.size() && pattern[p] == '*') { star = p++; mark = n; }
        else if (star != std::string::npos) { p = star + 1; n = ++mark; }
        else return false;
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// Files (not directories) in `dir` whose name matches `pattern`, as "sub\name" relative names; subfolders too when `recurse`.
void list_files(const std::filesystem::path& dir, const std::string& pattern, bool recurse, const std::string& prefix,
                std::vector<std::string>& out) {
    std::error_code ec;
    std::vector<std::filesystem::directory_entry> entries;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) entries.push_back(*it);
    for (const auto& e : entries) {
        const std::string name = e.path().filename().string();
        if (e.is_directory(ec)) {
            if (recurse) list_files(e.path(), pattern, true, prefix + name + "\\", out);
        } else if (wildcard_match(pattern, name)) {
            out.push_back(prefix + name);
        }
    }
}

std::optional<std::vector<std::uint8_t>> read_whole(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty()) return std::nullopt;
    return bytes;
}

// BGI.gdb status codes the script sees (80:80): 0 restored, 1 no usable file, 2 file damaged or unsupported
constexpr Word kGdbOk = 0, kGdbMissing = 1, kGdbDamaged = 2;

} // namespace

void register_system_subops(vm::SubopRegistry& r, SystemState& state, ArcResourceFinder& resources) {
    SystemState* s = &state;
    ArcResourceFinder* res = &resources;
    auto directory_index = std::make_shared<DirectoryIndex>();

    // the engine clock in milliseconds (low 32 bits): the scheduler's time
    add(r, 0x8004, "clock", 0, 1, [](Ctx& c) {
        c.push(static_cast<Word>(c.vm.now_ms));
        return 0;
    });
    add(r, 0x80FD, "launcher flag", 0, 1, [s](Ctx& c) {
        c.push(s->launcher ? 1 : 0);
        return 0;
    });
    add(r, 0x80E8, "game name", 1, 0, [s](Ctx& c) {
        const Word dst = c.pop();
        if (c.ok()) c.write_cstring(dst, s->game_name);
        return 0;
    });
    add(r, 0x8000, "random seed", 1, 0, [s](Ctx& c) {
        s->rand_seed = c.pop();
        return 0;
    });
    // 80:01 a 15-bit pseudo-random number; 80:02 (limit): three of them mixed, modulo the limit (0 for a limit <= 0).
    // The original's exact generator is not reproduced; scripts only see "random".
    auto next_rand = [s]() {
        s->rand_seed = s->rand_seed * 214013u + 2531011u;
        return (s->rand_seed >> 16) & 0x7FFFu;
    };
    add(r, 0x8001, "random", 0, 1, [next_rand](Ctx& c) { c.push(next_rand()); return 0; });
    add(r, 0x8002, "random below", 1, 1, [next_rand](Ctx& c) {
        const auto limit = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        const Word mixed = (next_rand() << 30) ^ (next_rand() << 15) ^ next_rand();
        c.push(limit > 0 ? mixed % static_cast<Word>(limit) : 0);
        return 0;
    });
    // Not modelled yet (the stack effect is known): 91:06 (2 values), 80:50 (a global), 90:E4/E5/E8/E9/E1/5C: effects
    // on display objects that are not drawn yet, 90:B7 (object, description) -> status 0, 90:E0 creates an object
    add(r, 0x9106, "91:06 (not modelled)", 2, 0, [](Ctx& c) { c.pop(), c.pop(); return 0; });
    add(r, 0x8050, "setting 80:50", 1, 0, [s](Ctx& c) { const Word v = c.pop(); if (c.ok()) s->values[0x8050] = v; return 0; });
    add(r, 0x90B7, "paint sprite panel buttons", 2, 1, [s](Ctx& c) {
        const Word desc = c.pop(), object = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_DBGPANEL"))
            std::fprintf(stderr, "90:B7 object %X desc %X: groups %d current %d\n", object, desc, int(c.read(desc, 4, false)), int(c.read(desc + 8, 4, false)));
        c.push(panel_paint(*s, c, object, desc, false));
        return 0;
    });
    // not modelled yet (display object / effect parameters); pop counts measured in the original: 90:3A 2, 90:53 5, 90:58 9,
    // 91:38 3, 91:9F 2, 92:90 15, 90:90 6 (none pushes anything). 80:1A (mouse region priority) pushes the logical input mask: 0 here
    for (const auto& [id, pops] : {std::pair{0x919Fu, 2}})
        add(r, static_cast<std::uint16_t>(id), "effect parameters (not modelled)", pops, 0, [pops](Ctx& c) { for (int i = 0; i < pops; ++i) c.pop(); return 0; });
    add(r, 0x9214, "92:14 wait object (not modelled)", 2, 0, [](Ctx& c) { c.pop(), c.pop(); return 0; });
    // 91:94 / 91:95 (two strings): a text (font name list) registry the original keeps; not modelled. 91:95 pushed 0 in the original.
    // 80:99 (number): removes an 80:98 item; pushes 0, or 1 when there is none
    add(r, 0x8099, "remove item", 1, 1, [s](Ctx& c) {
        const Word id = c.pop();
        if (!c.ok()) return 0;
        auto it = std::find_if(s->items.begin(), s->items.end(), [&](const auto& x) { return x.id == id; });
        if (it == s->items.end()) c.push(1);
        else s->items.erase(it), c.push(0);
        return 0;
    });

    // GetLocalTime: SYSTEMTIME = year, month, day of week, day, hour, minute, second, milliseconds (u16 each)
    add(r, 0x800C, "local time", 1, 0, [s](Ctx& c) {
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        std::array<std::uint16_t, 8> f = {std::uint16_t(tm.tm_year + 1900), std::uint16_t(tm.tm_mon + 1), std::uint16_t(tm.tm_wday),
                                    std::uint16_t(tm.tm_mday), std::uint16_t(tm.tm_hour), std::uint16_t(tm.tm_min),
                                    std::uint16_t(tm.tm_sec), 0};
        if (s->calendar_time) f = s->calendar_time();
        std::uint8_t bytes[16];
        for (int i = 0; i < 8; ++i) {
            bytes[2 * i] = std::uint8_t(f[i] & 255);
            bytes[2 * i + 1] = std::uint8_t(f[i] >> 8);
        }
        c.write_bytes(dst, bytes, sizeof bytes);
        return 0;
    });
    // total and available physical memory, each clamped to 0x7fffffff (the host's real numbers are not script data)
    add(r, 0x800D, "memory size", 0, 2, [](Ctx& c) {
        c.push(0x7FFFFFFF);
        c.push(0x7FFFFFFF);
        return 0;
    });
    add(r, 0x8070, "scratch buffer", 1, 1, [s](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        if (v > 0xC) {
            c.fail(Errc::Range, "scratch buffer size exponent " + std::to_string(v) + " out of range");
            return 0;
        }
        // the script global memory (tag 0): the saved settings of BGI.gdb live in its first 0x400 bytes
        c.vm.global_ready = true;
        s->scratch_size = std::size_t(0x1000) << v;
        c.vm.global.zero(0, s->scratch_size);
        c.push(1);
        return 0;
    });
    // (prefix, name): does the resource exist, as a file (an absolute path, or one relative to the game directory;
    // paths under the game directory are looked up in the data directory first) or as an archive member
    add(r, 0x8034, "resource exists", 2, 1, [s, res, directory_index](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        if (!c.ok()) return 0;
        std::error_code ec;
        bool found = false;
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "80:34 prefix=[%s] name=[%s]\n", prefix.c_str(), name.c_str());
        const std::string game_prefix = s->game_dir.string() + "\\";
        const bool absolute_game_path = !name.empty() && name.starts_with(game_prefix);
        if (absolute_game_path) {
            const auto redirected = host_path(*s, name);
            std::string normalized = name;
            std::replace(normalized.begin(), normalized.end(), '\\', '/');
            const std::filesystem::path original(normalized);
            found = directory_index->is_regular_file(redirected, c.vm.frame) ||
                    directory_index->is_regular_file(original, c.vm.frame);
            // A literal backslash can also occur in a POSIX filename.
            if (!found) found = directory_index->is_regular_file(std::filesystem::path(name), c.vm.frame);
            if (!found) found = res->exists_in_archives(prefix, name);
        } else {
            if (!name.empty()) {
                const std::filesystem::path as_given = host_path(*s, name);
                found = std::filesystem::is_regular_file(as_given, ec) || std::filesystem::is_regular_file(std::filesystem::path(name), ec);
                if (!found && std::filesystem::path(name).is_relative()) found = std::filesystem::is_regular_file(s->game_dir / name, ec);
            }
            if (!found) found = res->exists(prefix, name);
        }
        c.push(found ? 1 : 0);
        return 0;
    });
    // (destination, SDC block): unpacks an SDC block ("SDC FORMAT 1.00") to the destination and pushes the unpacked size; 0 when
    // the block is not SDC.
    add(r, 0x80C1, "unpack SDC", 2, 1, [](Ctx& c) {
        const Word src = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        std::vector<std::uint8_t> head;
        c.read_bytes(src, 0x20, head);
        if (!c.ok()) return 0;
        if (!is_sdc(ByteView(head.data(), head.size()))) {
            c.push(0);
            return 0;
        }
        const std::uint32_t csize = static_cast<std::uint32_t>(head[0x14] | head[0x15] << 8 | head[0x16] << 16 | std::uint32_t(head[0x17]) << 24);
        if (csize > 0x4000000u) {
            c.push(0);
            return 0;
        }
        std::vector<std::uint8_t> raw;
        c.read_bytes(src, std::size_t(0x20) + csize, raw);
        if (!c.ok()) return 0;
        auto sdc = sdc_decode(ByteView(raw.data(), raw.size()));
        if (!sdc) {
            c.push(0);
            return 0;
        }
        c.write_bytes(dst, sdc->data.data(), sdc->data.size());
        c.push(static_cast<Word>(sdc->data.size()));
        return 0;
    });
    // (destination, SDC block): unpacks an SDC block that holds a "DCFS FORMAT 1.00" container and expands it at the
    // destination: a base plane of N bytes, then M-1 planes of N bytes, each given as alternating runs (a varint length;
    // "same as the previous plane", then "literal", ...). Pushes M (the number of planes), 0 for a block that is not
    // SDC or not DCFS.
    // 80:C4 (dst, src, frame bytes n, frame count m): the encoder of 80:C5: "DCFS FORMAT 1.00", n, m, the first frame, then for
    // every later frame alternating runs (same as the previous frame, literal, ...; varint lengths, starting with "same"), all
    // wrapped as SDC. The original runs it as a wait object; here it finishes at once and pushes the encoded size, 0 on failure.
    add(r, 0x80C4, "encode DCFS", 4, 1, [](Ctx& c) {
        const Word frames = c.pop(), n = c.pop(), src = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        if (n == 0 || frames == 0 || std::uint64_t{n} * frames > 0x4000000u) {
            c.push(0);
            return 0;
        }
        std::vector<std::uint8_t> data;
        c.read_bytes(src, std::size_t{n} * frames, data);
        if (!c.ok()) return 0;
        std::vector<std::uint8_t> body(0x18);
        std::memcpy(body.data(), "DCFS FORMAT 1.00", 16);
        std::memcpy(body.data() + 0x10, &n, 4);
        std::memcpy(body.data() + 0x14, &frames, 4);
        body.insert(body.end(), data.begin(), data.begin() + n);
        const auto varint = [&](std::size_t v) {
            while (v >= 0x80) body.push_back(static_cast<std::uint8_t>(v | 0x80)), v >>= 7;
            body.push_back(static_cast<std::uint8_t>(v));
        };
        for (Word pass = 1; pass < frames; ++pass) {
            const std::uint8_t* prev = data.data() + std::size_t{pass - 1} * n;
            const std::uint8_t* cur = data.data() + std::size_t{pass} * n;
            std::size_t at = 0;
            bool literal = false;
            while (at < n) {
                std::size_t end = at;
                while (end < n && (cur[end] == prev[end]) != literal) ++end;
                varint(end - at);
                if (literal) body.insert(body.end(), cur + at, cur + end);
                at = end;
                literal = !literal;
            }
        }
        const std::vector<std::uint8_t> block = sdc_encode(ByteView(body.data(), body.size()));
        c.write_bytes(dst, block.data(), block.size());
        c.push(static_cast<Word>(block.size()));
        return 0;
    });
    add(r, 0x80C5, "expand DCFS", 2, 1, [](Ctx& c) {
        const Word src = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        std::vector<std::uint8_t> head;
        c.read_bytes(src, 0x20, head);
        if (!c.ok()) return 0;
        const ByteView hv(head.data(), head.size());
        if (!is_sdc(hv)) {
            c.push(0);
            return 0;
        }
        const std::uint32_t csize = static_cast<std::uint32_t>(head[0x14] | head[0x15] << 8 | head[0x16] << 16 | std::uint32_t(head[0x17]) << 24);
        if (csize > 0x4000000u) {
            c.push(0);
            return 0;
        }
        std::vector<std::uint8_t> raw;
        c.read_bytes(src, std::size_t(0x20) + csize, raw);
        if (!c.ok()) return 0;
        auto sdc = sdc_decode(ByteView(raw.data(), raw.size()));
        if (!sdc || sdc->data.size() < 0x18 || std::memcmp(sdc->data.data(), "DCFS FORMAT 1.00", 16) != 0) {
            c.push(0);
            return 0;
        }
        const std::vector<std::uint8_t>& in = sdc->data;
        const auto u32 = [&](std::size_t at) { return static_cast<std::uint32_t>(in[at] | in[at + 1] << 8 | in[at + 2] << 16 | std::uint32_t(in[at + 3]) << 24); };
        const std::uint32_t n = u32(0x10), planes = u32(0x14);
        if (n == 0 || planes == 0 || std::uint64_t(n) * planes > 0x4000000u || std::size_t(n) + 0x18 > in.size()) {
            c.push(0);
            return 0;
        }
        std::vector<std::uint8_t> out(std::size_t(n) * planes, 0);
        std::copy_n(in.begin() + 0x18, n, out.begin());
        std::size_t rd = std::size_t(n) + 0x18;
        for (std::uint32_t pass = 1; pass < planes; ++pass) {
            const std::size_t prev = std::size_t(pass - 1) * n, cur = std::size_t(pass) * n;
            std::uint32_t done = 0;
            bool literal = false;
            while (done < n) {
                std::uint32_t len = 0;
                for (unsigned shift = 0;; shift += 7) {
                    if (rd >= in.size()) { c.push(0); return 0; }
                    const std::uint8_t b = in[rd++];
                    len |= std::uint32_t(b & 0x7F) << (shift & 31);
                    if (!(b & 0x80)) break;
                }
                if (std::uint64_t(done) + len > n || (literal && rd + len > in.size())) { c.push(0); return 0; }
                if (literal) std::copy_n(in.begin() + std::ptrdiff_t(rd), len, out.begin() + std::ptrdiff_t(cur + done));
                else std::copy_n(out.begin() + std::ptrdiff_t(prev + done), len, out.begin() + std::ptrdiff_t(cur + done));
                if (literal) rd += len;
                done += len;
                literal = !literal;
            }
        }
        c.write_bytes(dst, out.data(), out.size());
        c.push(planes);
        return 0;
    });
    // (prefix, name): the size in bytes of the resource (a loose file or an archive member), 0 when it does not exist
    add(r, 0x8035, "resource size", 2, 1, [s, res](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        if (!c.ok()) return 0;
        std::error_code ec;
        std::uintmax_t size = 0;
        if (!name.empty()) {
            for (const std::filesystem::path& p : {host_path(*s, name), std::filesystem::path(name), s->game_dir / name}) {
                if (std::filesystem::is_regular_file(p, ec)) {
                    size = std::filesystem::file_size(p, ec);
                    break;
                }
            }
            if (size == 0)
                if (auto data = res->read(prefix, name)) size = data->size();
        }
        c.push(static_cast<Word>(size));
        return 0;
    });
    // (limit, recurse, pattern, max name length, buffer): lists the files matching a wildcard path such as
    // "...\UserData\*.sud". Names (NUL-terminated, one after another) go to the buffer; a buffer of 0 only counts.
    // Pushes the number of files (written / found). Saves written by OpenBGI and the game's own are both listed.
    add(r, 0x8025, "list files", 5, 1, [s](Ctx& c) {
        const Word limit = c.pop(), recurse = c.pop();
        std::string pattern = c.cstring(c.pop());
        const Word max_len = c.pop(), buffer = c.pop();
        if (!c.ok()) return 0;
        const std::filesystem::path mapped = host_path(*s, pattern);
        std::vector<std::string> names;
        for (const std::filesystem::path& full : {mapped, std::filesystem::path(std::u8string(pattern.begin(), pattern.end()))}) {
            std::vector<std::string> found;
            list_files(full.parent_path(), full.filename().string(), recurse != 0, "", found);
            std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
                return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                                    [](char x, char y) { return std::tolower(x) < std::tolower(y); });
            });
            for (auto& n : found)
                if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(std::move(n));
        }
        Word count = 0;
        std::vector<std::uint8_t> bytes;
        for (const std::string& n : names) {
            if (limit != 0 && count >= limit) break;
            if (buffer != 0 && max_len != 0 && n.size() > max_len) continue;
            ++count;
            bytes.insert(bytes.end(), n.begin(), n.end());
            bytes.push_back(0);
        }
        if (buffer != 0 && !bytes.empty()) c.write_bytes(buffer, bytes.data(), bytes.size());
        c.push(count);
        return 0;
    });
    // (prefix, name): deletes a file; only files OpenBGI itself wrote (the data directory) can be removed, the game's
    // own are read-only. Pushes 1 when a file was deleted.
    add(r, 0x8033, "delete file", 2, 1, [s, directory_index](Ctx& c) {
        directory_index->clear();
        std::string name = c.cstring(c.pop());
        const Word prefix_addr = c.pop();
        std::string prefix = prefix_addr ? c.cstring(prefix_addr) : std::string();
        if (!c.ok()) return 0;
        const std::filesystem::path target = host_path(*s, prefix + name);
        std::error_code ec;
        bool inside = false;
        if (auto rel = std::filesystem::relative(target, s->data_dir, ec); !ec && !rel.empty() && *rel.begin() != "..") inside = true;
        c.push(inside && std::filesystem::remove(target, ec) && !ec ? 1 : 0);
        return 0;
    });
    // (destination, archive, name, offset, size): copies `size` bytes starting at `offset` of a resource into script
    // memory. The resource is a loose file (data directory first, then the game directory) or a member of the named
    // archive (address 0 / unknown archive: any archive). The original may go asynchronous (return code 2); here it
    // is always done on the spot. Pushes 0.
    auto load_resource = [s, res](Ctx& c, const std::string& archive, const std::string& name, std::vector<std::uint8_t>& bytes) {
        std::error_code ec;
        for (const std::filesystem::path& p : {host_path(*s, name), s->game_dir / std::filesystem::path(std::u8string(name.begin(), name.end()))}) {
            if (!std::filesystem::is_regular_file(p, ec)) continue;
            if (auto data = read_whole(p)) {
                bytes = std::move(*data);
                return true;
            }
        }
        auto data = res->read(archive, name);
        if (!data) {
            c.fail(Errc::Io, "resource " + archive + "/" + name + " not found");
            return false;
        }
        bytes = std::move(*data);
        return true;
    };
    // (destination, archive, name): the whole resource into script memory (usually a block from 80:20). Pushes its size.
    add(r, 0x8030, "load resource", 3, 1, [load_resource](Ctx& c) {
        std::string name = c.cstring(c.pop());
        const Word archive_addr = c.pop();
        std::string archive = archive_addr ? c.cstring(archive_addr) : std::string();
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        std::vector<std::uint8_t> bytes;
        if (!load_resource(c, archive, name, bytes)) return 0;
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "80:30 dst=%X [%s] [%s] %zu bytes\n", dst, archive.c_str(), name.c_str(), bytes.size());
        if (dst != 0 && !bytes.empty()) c.write_bytes(dst, bytes.data(), bytes.size());
        c.push(static_cast<Word>(bytes.size()));
        return 0;
    });
    // (destination, archive, name, offset, size): a range of the resource into script memory (offset = size = 0: all of it).
    // Pushes 0 ok, 1 not found, 2 the range ends beyond the data, 3 size 0 or beyond (a missing file is not a script error).
    add(r, 0x8031, "load resource range", 5, 1, [s, res](Ctx& c) {
        const Word size = c.pop(), offset = c.pop();
        std::string name = c.cstring(c.pop());
        const Word archive_addr = c.pop();
        std::string archive = archive_addr ? c.cstring(archive_addr) : std::string();
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        std::vector<std::uint8_t> bytes;
        bool found = false;
        std::error_code ec;
        for (const std::filesystem::path& p : {host_path(*s, name), s->game_dir / std::filesystem::path(std::u8string(name.begin(), name.end()))}) {
            if (!std::filesystem::is_regular_file(p, ec)) continue;
            if (auto data = read_whole(p)) { bytes = std::move(*data); found = true; break; }
        }
        if (!found) {
            if (auto data = res->read(archive, name)) { bytes = std::move(*data); found = true; }
        }
        if (!found) { c.push(1); return 0; }
        std::size_t from = offset, count = size;
        if (offset == 0 && size == 0) count = bytes.size();
        else if (size == 0 || size > 0x4000000u) { c.push(3); return 0; }
        if (from + count > bytes.size()) { c.push(2); return 0; }
        if (dst && count) c.write_bytes(dst, bytes.data() + from, count);
        c.push(0);
        return 0;
    });
    add(r, 0x8130, "load resource bytes", 5, 1, [load_resource](Ctx& c) {
        const Word size = c.pop(), offset = c.pop();
        std::string name = c.cstring(c.pop());
        const Word archive_addr = c.pop();
        std::string archive = archive_addr ? c.cstring(archive_addr) : std::string();
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "81:30 dst=%X [%s] [%s] off=%u size=%u\n", dst, archive.c_str(), name.c_str(), offset, size);
        if (size > 0x4000000u) {
            c.fail(Errc::Range, "resource read of " + std::to_string(size) + " bytes is too large");
            return 0;
        }
        std::vector<std::uint8_t> bytes;
        if (!load_resource(c, archive, name, bytes)) return 0;
        if (offset < bytes.size() && size != 0 && dst != 0) {
            const std::size_t n = std::min<std::size_t>(size, bytes.size() - offset);
            c.write_bytes(dst, bytes.data() + offset, n);
        }
        c.push(0);
        return 0;
    });
    // (selector, destination): 0 = the game directory, 1 = the alternate path when there is one. Pushes 1 on success.
    add(r, 0x803D, "base path", 2, 1, [s](Ctx& c) {
        const Word which = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        const std::string* path = which == 0 ? &s->game_path : (which == 1 && !s->alt_path.empty() ? &s->alt_path : nullptr);
        if (path) c.write_cstring(dst, *path);
        c.push(path ? 1 : 0);
        return 0;
    });
    // is this path a directory (the data directory copy counts as well as the original)
    add(r, 0x802A, "is directory", 1, 1, [s](Ctx& c) {
        std::string path = c.cstring(c.pop());
        if (!c.ok()) return 0;
        std::error_code ec;
        bool dir = std::filesystem::is_directory(host_path(*s, path), ec);
        if (!dir) dir = std::filesystem::is_directory(std::filesystem::path(path), ec);
        c.push(dir ? 1 : 0);
        return 0;
    });
    add(r, 0x8028, "create directory", 1, 1, [s, directory_index](Ctx& c) {
        directory_index->clear();
        std::string path = c.cstring(c.pop());
        if (!c.ok()) return 0;
        std::error_code ec;
        const auto target = host_path(*s, path);
        const bool made = std::filesystem::create_directory(target, ec) && !ec;
        c.push(made ? 1 : 0);
        return 0;
    });
    // the window-theme helper loaded at run time is not present on the target system: report failure
    add(r, 0x8118, "window helper", 1, 1, [](Ctx& c) {
        c.pop();
        c.push(1); // the real game answers 1
        return 0;
    });
    // BGI.gdb: our own saves are written uncompressed into the data directory; the game's original (SDC packed) file
    // next to the executable is still read when there is no save of ours. Pushes window left, top, status.
    add(r, 0x8080, "restore global data", 0, 3, [s](Ctx& c) {
        auto file = read_whole(s->data_dir / "BGI.gdb");
        if (!file) file = read_whole(s->game_dir / "BGI.gdb");
        if (!file) { // the original leaves both numbers unset here
            c.push(0); c.push(0); c.push(kGdbMissing);
            return 0;
        }
        std::vector<std::uint8_t> decoded;
        if (is_sdc(*file)) {
            auto d = sdc_decode(*file);
            if (!d) { c.push(0); c.push(0); c.push(kGdbDamaged); return 0; }
            decoded = std::move(d->data);
        } else {
            decoded = std::move(*file);
        }
        auto g = gdb_decode(decoded);
        if (!g) { c.push(0); c.push(0); c.push(kGdbDamaged); return 0; }
        s->gdb = std::move(*g);
        const std::size_t keep = std::min(s->gdb.settings.size(), s->scratch_size);
        if (keep) c.vm.global.write(0, keep, s->gdb.settings.data());
        c.push(s->gdb.left);
        c.push(s->gdb.top);
        c.push(kGdbOk);
        return 0;
    });
    // save BGI.gdb. The original returns an unset register here, so the value pushed (0) carries no information.
    add(r, 0x8081, "save global data", 0, 1, [s, directory_index](Ctx& c) {
        directory_index->clear();
        const std::size_t keep = std::min(s->scratch_size, s->gdb.settings.size());
        if (keep) c.vm.global.read(0, keep, s->gdb.settings.data());
        const auto bytes = gdb_encode(s->gdb);
        std::error_code ec;
        std::filesystem::create_directories(s->data_dir, ec);
        std::ofstream out(s->data_dir / "BGI.gdb", std::ios::binary | std::ios::trunc);
        if (out) out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        c.push(out ? 1 : 0); // the original pushes the WriteFile result
        return 0;
    });
    // 80:82 (size, address, offset): script memory -> global data; 80:83 (size, offset, address): global data -> script memory
    auto gdb_range = [](Ctx& c, Word offset, Word size) {
        const std::uint64_t end = std::uint64_t(offset) + size;
        if (offset >= GdbData::kAreaSize || size == 0 || size > GdbData::kAreaSize || end > GdbData::kAreaSize) {
            c.fail(Errc::Range, "global data range offset " + std::to_string(offset) + " size " + std::to_string(size) + " out of bounds");
            return false;
        }
        return true;
    };
    add(r, 0x8082, "write global data", 3, 0, [s, gdb_range](Ctx& c) {
        const Word size = c.pop(), addr = c.pop(), offset = c.pop();
        if (!c.ok() || !gdb_range(c, offset, size)) return 0;
        std::vector<std::uint8_t> bytes;
        c.read_bytes(addr, size, bytes);
        if (c.ok()) std::copy(bytes.begin(), bytes.end(), s->gdb.area.begin() + offset);
        return 0;
    });
    add(r, 0x8083, "read global data", 3, 0, [s, gdb_range](Ctx& c) {
        const Word size = c.pop(), offset = c.pop(), addr = c.pop();
        if (!c.ok() || !gdb_range(c, offset, size)) return 0;
        c.write_bytes(addr, s->gdb.area.data() + offset, size);
        return 0;
    });
    // (block, byte size): loads the word table from a script block: u32 entry count, then per entry a name, a u32 pair
    // count and that many (key, value) string pairs, all strings zero-terminated. Block 0 clears the table.
    // Pushes 1 when the entries use exactly `size` bytes.
    // 81:DA, popped in this order: the word to translate, the table entry name, an optional output address. Looks the
    // word up in the entry loaded by 81:D8 (FUN_0049b3c0 -> FUN_004a9290); copies the translation to the address and
    // pushes its length, or pushes 0 when there is none.
    add(r, 0x81DA, "translate word", 3, 1, [s](Ctx& c) {
        const std::string word = c.cstring(c.pop());
        const std::string entry = c.cstring(c.pop());
        const Word out = c.pop();
        if (!c.ok()) return 0;
        const std::string* found = nullptr;
        if (auto it = s->dictionary.find(entry); it != s->dictionary.end())
            for (const auto& [key, value] : it->second)
                if (key == word) { found = &value; break; }
        if (found && out != 0) c.write_cstring(out, *found);
        c.push(found ? static_cast<Word>(found->size()) : 0);
        return 0;
    });
    add(r, 0x81D8, "load word table", 2, 1, [s](Ctx& c) {
        const Word size = c.pop(), block = c.pop();
        if (!c.ok()) return 0;
        if (block == 0) {
            s->dictionary.clear();
            c.push(1);
            return 0;
        }
        std::vector<std::uint8_t> bytes;
        c.read_bytes(block, size, bytes);
        if (!c.ok()) return 0;
        std::size_t pos = 0;
        bool good = true;
        auto u32 = [&]() -> Word {
            if (pos + 4 > bytes.size()) { good = false; return 0; }
            const Word v = load_u32le(bytes.data() + pos);
            pos += 4;
            return v;
        };
        auto str = [&]() -> std::string {
            const auto end = std::find(bytes.begin() + std::ptrdiff_t(std::min(pos, bytes.size())), bytes.end(), std::uint8_t{0});
            if (end == bytes.end()) { good = false; return {}; }
            std::string out(bytes.begin() + std::ptrdiff_t(pos), end);
            pos = std::size_t(end - bytes.begin()) + 1;
            return out;
        };
        std::map<std::string, std::vector<std::pair<std::string, std::string>>> table;
        const Word entries = u32();
        for (Word i = 0; i < entries && good; ++i) {
            std::string name = str();
            const Word pairs = u32();
            auto& list = table[name];
            for (Word k = 0; k < pairs && good; ++k) {
                std::string key = str();
                std::string value = str();
                list.emplace_back(std::move(key), std::move(value));
            }
        }
        if (good) s->dictionary = std::move(table);
        c.push(good && pos == size ? 1 : 0);
        return 0;
    });
    // record a key string in the global list (once); 1 = done
    add(r, 0x8084, "add global key", 1, 1, [s](Ctx& c) {
        std::string key = c.cstring(c.pop());
        if (!c.ok()) return 0;
        auto& keys = *s->string_set(SystemState::kGlobalSet, true);
        if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(std::move(key));
        c.push(1);
        return 0;
    });
    add(r, 0x8085, "has global key", 1, 1, [s](Ctx& c) {
        std::string key = c.cstring(c.pop());
        if (!c.ok()) return 0;
        const auto& keys = *s->string_set(SystemState::kGlobalSet, true);
        c.push(std::find(keys.begin(), keys.end(), key) != keys.end() ? 1 : 0);
        return 0;
    });
    // 80:D8: drops every list except the global one
    add(r, 0x80D8, "drop string lists", 0, 0, [s](Ctx&) {
        s->string_sets.clear();
        return 0;
    });
    // (list id) -> number of strings (0 when there is no such list)
    add(r, 0x80D9, "string list size", 1, 1, [s](Ctx& c) {
        const Word id = c.pop();
        if (!c.ok()) return 0;
        const auto* set = s->string_set(id, false);
        c.push(set ? static_cast<Word>(set->size()) : 0);
        return 0;
    });
    // (list id, count, strings): replaces a list by `count` zero-terminated strings stored one after another;
    // count 0 empties it. 1 = done, 0 = bad count or no string block
    add(r, 0x80DA, "set string list", 3, 1, [s](Ctx& c) {
        const Word addr = c.pop(), count = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (count == 0) {
            if (id == SystemState::kGlobalSet) s->gdb.keys.clear();
            else s->string_sets.erase(id);
            c.push(1);
            return 0;
        }
        if (static_cast<std::int32_t>(count) < 0 || addr == 0) {
            c.push(0);
            return 0;
        }
        std::vector<std::string> list;
        Word at = addr;
        for (Word i = 0; i < count; ++i) {
            list.push_back(c.cstring(at));
            if (!c.ok()) return 0;
            at += static_cast<Word>(list.back().size()) + 1;
        }
        *s->string_set(id, true) = std::move(list);
        c.push(1);
        return 0;
    });
    // (destination, list id): copies the strings, NUL terminated, one after another; destination 0 only measures.
    // Pushes the byte count (0 for an unknown list)
    add(r, 0x80DB, "get string list", 2, 1, [s](Ctx& c) {
        const Word id = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        const auto* set = s->string_set(id, false);
        std::vector<std::uint8_t> bytes;
        if (set)
            for (const auto& str : *set) {
                bytes.insert(bytes.end(), str.begin(), str.end());
                bytes.push_back(0);
            }
        if (dst != 0 && !bytes.empty()) c.write_bytes(dst, bytes.data(), bytes.size());
        c.push(static_cast<Word>(bytes.size()));
        return 0;
    });
    // (destination, list id, index): copies one string of a list, NUL terminated. 0 = done, 0x80000001 = no such list,
    // 0x80000002 = index out of range
    add(r, 0x80DD, "get string list item", 3, 1, [s](Ctx& c) {
        const Word index = c.pop(), id = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        const auto* set = s->string_set(id, false);
        if (!set) c.push(0x80000001u);
        else if (index >= set->size()) c.push(0x80000002u);
        else {
            c.write_cstring(dst, (*set)[index]);
            c.push(0);
        }
        return 0;
    });
    // (list id, string): adds the string to a list (creating it) unless it is already there; pushes its index
    add(r, 0x80DC, "add to string list", 2, 1, [s](Ctx& c) {
        const Word str_addr = c.pop(), id = c.pop();
        if (!c.ok()) return 0;
        if (str_addr == 0) {
            c.push(0xFFFFFFFFu);
            return 0;
        }
        std::string str = c.cstring(str_addr);
        if (!c.ok()) return 0;
        auto& list = *s->string_set(id, true);
        auto it = std::find(list.begin(), list.end(), str);
        if (it == list.end()) it = list.insert(list.end(), std::move(str));
        c.push(static_cast<Word>(it - list.begin()));
        return 0;
    });
    // (destination, source, size): folds `size` bytes at source into the 8-byte hash state at destination
    add(r, 0x81E9, "rolling hash", 3, 0, [](Ctx& c) {
        const Word size = c.pop(), src = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        std::vector<std::uint8_t> data, st;
        c.read_bytes(src, size, data);
        c.read_bytes(dst, 8, st);
        if (!c.ok()) return 0;
        std::uint32_t a = load_u32le(st.data());
        for (std::uint8_t b : data) {
            a = std::uint32_t(b) + a * 0xE9u;
            const std::uint8_t lo = std::uint8_t(a);
            st[4] = std::uint8_t(st[4] + lo);
            st[5] = std::uint8_t(st[5] ^ lo);
            st[6] = std::uint8_t(st[6] + b);
            st[7] = std::uint8_t(st[7] ^ b);
        }
        for (int i = 0; i < 4; ++i) st[i] = std::uint8_t(a >> (8 * i));
        c.write_bytes(dst, st.data(), 8);
        return 0;
    });
    // (source, destination): CP932 string -> UTF-8 (the caller sizes the destination)
    add(r, 0x8121, "cp932 to utf8", 2, 0, [](Ctx& c) {
        std::string src = c.cstring(c.pop());
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        std::string utf8;
        for (char32_t ch : cp932_to_utf32(src)) {
            if (ch < 0x80) utf8 += char(ch);
            else if (ch < 0x800) utf8 += {char(0xC0 | (ch >> 6)), char(0x80 | (ch & 0x3F))};
            else if (ch < 0x10000) utf8 += {char(0xE0 | (ch >> 12)), char(0x80 | ((ch >> 6) & 0x3F)), char(0x80 | (ch & 0x3F))};
            else utf8 += {char(0xF0 | (ch >> 18)), char(0x80 | ((ch >> 12) & 0x3F)), char(0x80 | ((ch >> 6) & 0x3F)), char(0x80 | (ch & 0x3F))};
        }
        c.write_cstring(dst, utf8);
        return 0;
    });
    add(r, 0x8037, "add resource search path", 1, 0, [s, res](Ctx& c) {
        std::string path = c.cstring(c.pop());
        if (c.ok()) {
            res->add_search_path(std::filesystem::path(cp932_to_utf32(path)));
            s->search_paths.insert(s->search_paths.begin(), std::move(path));
        }
        return 0;
    });
    // (a, b, index): remembers two numbers per index below 16. 1 = bad index, 2 = a zero number, 0 = stored
    add(r, 0x8160, "pair table", 3, 1, [s](Ctx& c) {
        const Word a = c.pop(), b = c.pop(), idx = c.pop();
        if (!c.ok()) return 0;
        if (idx >= 16) c.push(1);
        else if (a == 0 || b == 0) c.push(2);
        else {
            s->pair_table[idx] = {b, a};
            c.push(0);
        }
        return 0;
    });
    add_setter(r, state, 0x8166, "resizable window"); // stored; native window style is not updated yet
    // 81:6C accepts sizes matching the desktop aspect ratio; the renderer currently keeps its own size.
    add(r, 0x816C, "render size", 2, 1, [s](Ctx& c) {
        auto h = static_cast<std::int32_t>(c.pop());
        auto w = static_cast<std::int32_t>(c.pop());
        if (!c.ok()) return 0;
        if (w <= 0 || h <= 0) w = static_cast<std::int32_t>(s->display_width), h = static_cast<std::int32_t>(s->display_height);
        const bool valid = h > 0 && s->display_height > 0 &&
            std::fabs(float(s->display_width) / float(s->display_height) - float(w) / float(h)) <= 0.001f;
        if (valid) s->records[0x816C] = {static_cast<Word>(w), static_cast<Word>(h)};
        c.push(valid ? 1 : 0);
        return 0;
    });
    add(r, 0x8100, "string code page", 1, 1, [s](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        if (v == 0) s->code_page = 932;
        else if (v == 1) s->code_page = 65001;
        c.push(v <= 1 ? 1 : 0);
        return 0;
    });
    add(r, 0x9002, "frame rate", 1, 0, [s](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        if (v > 1000) {
            c.fail(Errc::Range, "frame rate " + std::to_string(v) + " out of range");
            return 0;
        }
        s->frame_ms = std::max<Word>(1, v ? 1000 / v : 1);
        return 0;
    });
    // pop a value and push ceil-ish 0x10000 / v (the 16.16 type-size scale; 0 stays 0)
    add(r, 0x9199, "type scale", 1, 1, [s](Ctx& c) {
        const Word v = c.pop();
        if (!c.ok()) return 0;
        const Word scale = v ? (v + 0xFFFF) / v : 0;
        s->values[0x9199] = scale;
        c.push(1); // the original always reports success; the scale only goes to a global
        return 0;
    });
    // (key, value): typesetting switches; the keys are listed in analysis/排版引擎.md
    add(r, 0x919A, "typesetting option", 2, 0, [s](Ctx& c) {
        const Word value = c.pop(), key = c.pop();
        if (c.ok()) s->typesetting[key] = value;
        return 0;
    });

    // ---- input / window state queries (verified against the original handlers, analysis/out/t_verify.tsv) ----
    add(r, 0x8017, "input skip state", 0, 1, [s](Ctx& c) { c.push(input_skip_state(*s)); return 0; });
    add(r, 0x8014, "set input skip permission", 1, 0, [s](Ctx& c) {
        const Word v = c.pop();
        if (c.ok()) s->skip_enabled = v != 0, s->values[0x8014] = v;
        return 0;
    });
    add(r, 0x801E, "swap mouse buttons", 1, 1, [s](Ctx& c) {
        const Word value = c.pop();
        if (!c.ok()) return 0;
        c.push(s->set_mouse_button_swap(value) ? 1u : 0u);
        return 0;
    });
    add(r, 0x800F, "window active", 0, 1, [](Ctx& c) { c.push(1); return 0; });
    // 92:F0 (6 values, two of them addresses): makes a video window of the movie cluster (analysis/视频.md) and pushes a result;
    // there is none yet, so it answers 0. 90:F3 (volume 0..0x80) and 90:01 (a value kept in a display global) only take their argument.
    add(r, 0x92F0, "movie window (not modelled)", 6, 1, [](Ctx& c) { for (int i = 0; i < 6; ++i) c.pop(); c.push(0); return 0; });
    // 90:F0 (file, x, y, w, h) -> length: the reference starts playing a loose movie file in the window and answers its length in ms, 0 when
    // playback could not start. There is no overlay player here (91:F8 plays a whole movie at once), so playback never starts: it answers 0.
    add(r, 0x90F0, "play loose movie (not modelled)", 5, 1, [](Ctx& c) {
        const Word h = c.pop(), w = c.pop();
        c.pop(), c.pop(), c.pop();
        if (!c.ok()) return 0;
        if (static_cast<std::int32_t>(w) <= 0 || static_cast<std::int32_t>(h) <= 0) { c.fail(Errc::Range, "invalid movie size"); return 0; }
        c.push(0);
        return 0;
    });
    // C0:F0 (out, &count, arc, name, base) -> result: loads a "bwef" lip-sync file of an archive (vecmaps.c: LoadBwef, ParseBwef) as
    // (offset + base, common) pairs into out and its count into *count; 0, or 0x80000001 for a missing or empty file, 0x80000002 for a bad
    // magic, 0x80000003 for a size that does not match the count. Never a script error.
    add(r, 0xC0F0, "load bwef", 5, 1, [res](Ctx& c) {
        const Word base = c.pop();
        const std::string name = c.cstring(c.pop()), arc = c.cstring(c.pop());
        const Word count_addr = c.pop(), out = c.pop();
        if (!c.ok()) return 0;
        auto file = res->read(arc, name);
        if (!file || file->empty()) { c.push(0x80000001u); return 0; }
        const std::vector<std::uint8_t>& d = *file;
        const auto u32 = [&d](std::size_t at) { return at + 4 <= d.size() ? std::uint32_t{d[at]} | std::uint32_t{d[at + 1]} << 8 | std::uint32_t{d[at + 2]} << 16 | std::uint32_t{d[at + 3]} << 24 : 0u; };
        if (d.size() < 0x20 || std::memcmp(d.data(), "bwef    ", 8) != 0) { c.push(0x80000002u); return 0; }
        const std::uint32_t count = u32(0x14);
        if (d.size() != 0x120 + 4 * std::size_t{count}) { c.push(0x80000003u); return 0; }
        for (std::uint32_t i = 0; i < count; ++i) {
            c.write(out + 8 * i, u32(0x120 + 4 * std::size_t{i}) + base, 4);
            c.write(out + 8 * i + 4, u32(0x18), 4);
        }
        c.write(count_addr, count, 4);
        c.push(0);
        return 0;
    });
    add(r, 0x9001, "display enable", 1, 0, [s](Ctx& c) {
        const Word on = c.pop();
        if (c.ok()) s->display_enabled = on != 0, s->values[0x9001] = on;
        return 0;
    });
    // 91:10..1E (except 91:1C): bitmap effects (stretch, vector graphics, bend, blend by degree, transforms); the pop counts are
    // known, the effects are not drawn yet
    for (auto [id, n] : {std::pair{0x9117, 6},
                         {0x9118, 11}, {0x911A, 3}})
        add(r, static_cast<std::uint16_t>(id), "bitmap effect (not drawn)", n, 0, [n](Ctx& c) { for (int i = 0; i < n; ++i) c.pop(); return 0; });
    add(r, 0x8010, "input enable", 1, 0, [s](Ctx& c) {
        const Word enabled = c.pop();
        if (c.ok()) s->set_input_enabled(enabled != 0);
        return 0;
    });
    add(r, 0x8016, "input flush", 0, 0, [s](Ctx&) {
        s->skip_flushed = true;
        for (Word vk : s->input_numbers(0x80000000)) (void)consume_input_trigger(*s, vk);
        return 0;
    });
    // 80:13: the input event serial, bumped on each new key/button press and wheel event.
    add(r, 0x8013, "input serial", 0, 1, [s](Ctx& c) { c.push(s->input_serial); return 0; });
    add(r, 0x8069, "close main window", 0, 0, [s](Ctx&) { s->quit_requested = true; return 0; });
    // 80:C0 (dst, src, size) -> encoded size: SDC-encodes the data (the original did it on a worker thread and pushed the size when done)
    add(r, 0x80C0, "SDC encode", 3, 1, [](Ctx& c) {
        const Word size = c.pop(), src = c.pop(), dst = c.pop();
        if (!c.ok()) return 0;
        if (size > 0x4000000u) { c.push(0); return 0; }
        std::vector<std::uint8_t> raw;
        c.read_bytes(src, size, raw);
        if (!c.ok()) return 0;
        const auto enc = sdc_encode(ByteView(raw.data(), raw.size()));
        c.write_bytes(dst, enc.data(), enc.size());
        c.push(static_cast<Word>(enc.size()));
        return 0;
    });
    add(r, 0x8006, "profiler switch", 1, 0, [](Ctx& c) { c.pop(); return 0; });
    add(r, 0x8007, "profiler figure", 2, 0, [](Ctx& c) { c.pop(); const Word out = c.pop(); if (c.ok() && out) c.write(out, 0, 4); return 0; });
    // 80:2B (drive, dir, fname, ext, path) -> ok: _splitpath of a Windows path; any destination may be 0
    add(r, 0x802B, "split path", 5, 1, [](Ctx& c) {
        const Word path = c.pop(), ext = c.pop(), fname = c.pop(), dir = c.pop(), drive = c.pop();
        if (!c.ok()) return 0;
        if (path == 0) { c.push(0); return 0; }
        std::string p = c.cstring(path), d, dr, name, e;
        std::size_t i = 0;
        if (p.size() >= 2 && p[1] == ':') dr = p.substr(0, 2), i = 2;
        const std::size_t sep = p.find_last_of("\\/");
        const std::size_t name_start = sep == std::string::npos || sep < i ? i : sep + 1;
        d = p.substr(i, name_start - i);
        name = p.substr(name_start);
        if (const std::size_t dot = name.find_last_of('.'); dot != std::string::npos) e = name.substr(dot), name.resize(dot);
        if (drive) c.write_cstring(drive, dr);
        if (dir) c.write_cstring(dir, d);
        if (fname) c.write_cstring(fname, name);
        if (ext) c.write_cstring(ext, e);
        c.push(1);
        return 0;
    });
    // 80:3A (buf, which): a system folder (0 Windows, 1 desktop, 2 programs, 3 documents); the user data directory stands in for all
    add(r, 0x803A, "special folder", 2, 0, [s](Ctx& c) {
        c.pop();
        const Word buf = c.pop();
        if (c.ok() && buf) c.write_cstring(buf, s->data_dir.string() + "\\");
        return 0;
    });
    add(r, 0x803B, "file dialog", 6, 1, [](Ctx& c) { for (int i = 0; i < 6; ++i) c.pop(); c.push(0xFFFFFFFFu); return 0; }); // always cancelled
    add(r, 0x803F, "install paths", 4, 1, [](Ctx& c) { for (int i = 0; i < 4; ++i) c.pop(); c.push(0); return 0; });
    add(r, 0x80E1, "run on exit", 3, 0, [](Ctx& c) { for (int i = 0; i < 3; ++i) c.pop(); return 0; });
    add(r, 0x80E3, "shell open", 1, 1, [](Ctx& c) { c.pop(); c.push(0); return 0; });
    add(r, 0x8119, "touch points", 1, 1, [](Ctx& c) { c.pop(); c.push(0); return 0; });
    add(r, 0x81F7, "create shortcut", 4, 1, [](Ctx& c) { for (int i = 0; i < 4; ++i) c.pop(); c.push(0); return 0; });
    add(r, 0x90F1, "stop movie", 0, 0, [](Ctx&) { return 0; }); // movies are not played
    add(r, 0x8015, "skip latch", 1, 0, [s](Ctx& c) {
        const Word v = c.pop();
        if (c.ok()) s->skip_latch = v != 0, s->values[0x8015] = v;
        return 0;
    });
    // 80:39 (dir): the alternative game directory (path selector 1 of 80:3D); 1 when the directory exists
    add(r, 0x8039, "alternative directory", 1, 1, [s](Ctx& c) {
        const std::string dir = c.cstring(c.pop());
        if (!c.ok()) return 0;
        std::error_code ec;
        const bool ok = !dir.empty() && std::filesystem::is_directory(std::filesystem::path(std::u8string(dir.begin(), dir.end())), ec);
        if (ok) s->alt_path = dir + (dir.back() == '\\' || dir.back() == '/' ? "" : "\\");
        c.push(ok ? 1 : 0);
        return 0;
    });
    // 80:61: the mode the display was set up with (the argument of the original's Direct3D set-up): 1 full screen, 0 windowed
    add(r, 0x8061, "display set-up mode", 0, 1, [](Ctx& c) { c.push(0); return 0; });
    // 92:15 only calls a flush routine in the original (no arguments, no result); 90:43 / 90:28 take 9 / 12 values for
    // grey-bitmap effects and a motion wait object: not modelled yet
    add(r, 0x9215, "flush 92:15", 0, 0, [](Ctx&) { return 0; });
    // 90:BC / 90:BF (number, address): read an icon control into a record and push 1, or push 0 when the number does not exist
    // (record contents checked against the original). 90:BC: 6 words, 1 followed by the UTF-16 text "877.in". 90:BF: 3 words, the
    // control's first two reads give a pending event (0x10000002, -1, 0), later ones all zeros.
    add(r, 0x90BC, "icon control info", 2, 1, [s](Ctx& c) {
        const Word num = c.pop(), addr = c.pop();
        if (!c.ok()) return 0;
        const bool found = s->icon_controls.count(num) != 0;
        if (found) {
            const auto& info = s->icon_controls[num].info;
            for (int k = 0; k < 6; ++k) c.write(addr + 4 * k, info[k], 4);
        }
        c.push(found);
        return 0;
    });
    // 90:BD (out, number): the index of the current group (-1 none); pushes 1 for a panel that exists
    add(r, 0x90BD, "icon control current group", 2, 1, [s](Ctx& c) {
        const Word num = c.pop(), addr = c.pop();
        if (!c.ok()) return 0;
        auto it = s->icon_controls.find(num);
        if (it != s->icon_controls.end()) c.write(addr, static_cast<Word>(it->second.current_group), 4);
        c.push(it != s->icon_controls.end());
        return 0;
    });
    // 90:BE (out, number): the selected button of every group (-1 none), one word each; pushes 1 for a panel that exists
    add(r, 0x90BE, "icon control selections", 2, 1, [s](Ctx& c) {
        const Word num = c.pop(), addr = c.pop();
        if (!c.ok()) return 0;
        auto it = s->icon_controls.find(num);
        if (it != s->icon_controls.end())
            for (std::size_t g = 0; g < it->second.groups.size(); ++g)
                c.write(addr + 4 * static_cast<Word>(g), static_cast<Word>(it->second.groups[g].selected), 4);
        c.push(it != s->icon_controls.end());
        return 0;
    });
    add(r, 0x90BF, "icon control event", 2, 1, [s](Ctx& c) {
        const Word num = c.pop(), addr = c.pop();
        if (!c.ok()) return 0;
        auto it = s->icon_controls.find(num);
        if (it != s->icon_controls.end()) {
            auto& ctl = it->second;
            std::array<Word, 3> ev{};
            if (!ctl.events.empty()) {
                ev = ctl.events.front();
                ctl.events.pop_front();
            } else if (ctl.reads++ < 2) {
                ev = {0x10000002u, 0xFFFFFFFFu, 0};
            }
            if (std::getenv("OBGI_TRACEPANEL") && num <= 32 && (ev[0] || ctl.reads <= 3)) {
                const auto f4 = static_cast<Word>(c.read(0x80F4, 4, false));
                const auto f8 = static_cast<Word>(c.read(0x80F8, 4, false));
                std::fprintf(stderr, "PANEL poll t%u ip=%X control=%u event=%X %X %X remaining=%zu reads=%u info0=%u locals[F4=%X,F8=%X] sp=%X calls=%zu",
                             c.t.id, c.t.ip, num, ev[0], ev[1], ev[2], ctl.events.size(), ctl.reads, ctl.info[0], f4, f8, c.t.sp, c.t.calls.size());
                for (std::size_t n = c.t.calls.size() > 3 ? c.t.calls.size() - 3 : 0; n < c.t.calls.size(); ++n)
                    std::fprintf(stderr, " [%X->%X]", c.t.calls[n].origin, c.t.calls[n].ret);
                std::fputc('\n', stderr);
            }
            if (std::getenv("OBGI_DBGPANEL") && ev[0]) std::fprintf(stderr, "90:BF control %u event %X %X %X\n", num, ev[0], ev[1], ev[2]);
            for (int k = 0; k < 3; ++k) c.write(addr + 4 * k, ev[k], 4);
        }
        c.push(it != s->icon_controls.end());
        return 0;
    });
    // 80:32 (name, data, size): creates or overwrites a file with the bytes; pushes 1 when all were written. Files inside the game
    // folder go to the data directory (host_path); anything that would land elsewhere is refused (pushes 0).
    add(r, 0x8032, "write file", 3, 1, [s, directory_index](Ctx& c) {
        directory_index->clear();
        const Word size = c.pop(), data = c.pop(), name_addr = c.pop();
        if (!c.ok()) return 0;
        const std::string name = name_addr ? c.cstring(name_addr) : std::string();
        std::vector<std::uint8_t> bytes;
        if (size) c.read_bytes(data, size, bytes);
        if (!c.ok()) return 0;
        const std::filesystem::path p = host_path(*s, name);
        bool ok = false;
        if (!name.empty() && !s->data_dir.empty() && p.generic_string().starts_with(s->data_dir.generic_string())) {
            std::error_code ec;
            std::filesystem::create_directories(p.parent_path(), ec);
            if (std::ofstream f(p, std::ios::binary); f) {
                f.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
                ok = bool(f);
            }
        }
        c.push(ok ? 1 : 0);
        return 0;
    });
    // 81:2F (directory): 1 when a file can be created there (the original makes and deletes a temporary file), else 0. The
    // scripts ask it about their user-data folder; here: the directory exists on the host (creating it when it is the data dir).
    add(r, 0x812F, "directory writable", 1, 1, [s, directory_index](Ctx& c) {
        directory_index->clear();
        const Word addr = c.pop();
        if (!c.ok()) return 0;
        const std::string name = addr ? c.cstring(addr) : std::string();
        std::error_code ec;
        const std::filesystem::path p = host_path(*s, name);
        bool ok = std::filesystem::is_directory(p, ec);
        if (std::getenv("OBGI_DBGPANEL")) std::fprintf(stderr, "81:2F dir '%s' -> %s ok=%d\n", name.c_str(), p.string().c_str(), ok);
        if (!ok && !name.empty() && !s->data_dir.empty() && p.generic_string().starts_with(s->data_dir.generic_string()))
            ok = std::filesystem::create_directories(p, ec) || std::filesystem::is_directory(p, ec); // our own data folder: make it
        c.push(ok ? 1 : 0);
        return 0;
    });
    // 81:35 (path, 0: pushed first by every script call): the size in bytes of a file, 0 when there is none (checked against the
    // original: AmairoChocolate2089.sud gives 0x12600B, its length). Loose files are looked up like 80:30 does.
    add(r, 0x8135, "file size", 2, 1, [s](Ctx& c) {
        const Word path_addr = c.pop();
        c.pop();
        if (!c.ok()) return 0;
        const std::string name = path_addr ? c.cstring(path_addr) : std::string();
        std::error_code ec;
        Word size = 0;
        for (const std::filesystem::path& p : {host_path(*s, name), s->game_dir / std::filesystem::path(std::u8string(name.begin(), name.end()))}) {
            if (std::filesystem::is_regular_file(p, ec)) {
                size = static_cast<Word>(std::filesystem::file_size(p, ec));
                break;
            }
        }
        c.push(size);
        return 0;
    });
    // 80:AC (control number, word count, address): sends a message to a control. Messages (0x10000002, ?, item) only move its
    // cursor; (0x10000000, item, 0) is a decision: the control answers it with the event (0x10000006, item, 0) and its info
    // record becomes (1, 0, item, 0, 0, 0) (all checked against the original). Pushes 1 when the control exists.
    add(r, 0x80AC, "send icon control message", 3, 1, [s](Ctx& c) {
        const Word addr = c.pop(), count = c.pop(), num = c.pop();
        if (!c.ok()) return 0;
        auto it = s->icon_controls.find(num);
        if (it != s->icon_controls.end() && count >= 3 && count <= 256) {
            Word w[3];
            for (int k = 0; k < 3; ++k) w[k] = static_cast<Word>(c.read(addr + 4 * k, 4, false));
            if (!c.ok()) return 0;
            if (std::getenv("OBGI_DBGPANEL")) std::fprintf(stderr, "80:AC control %u msg %X %X %X\n", num, w[0], w[1], w[2]);
            if (w[0] == 0x10000000) {
                panel_decide(*s, num, static_cast<std::int16_t>(w[1] >> 16), static_cast<std::int16_t>(w[1] & 0xFFFF), w[2]);
            } else if (w[0] == 0x10000001) {
                panel_set_group(*s, num, static_cast<int>(w[1]));
            } else if (w[0] == 0x10000002) {
                panel_select(*s, num, static_cast<int>(w[1]), static_cast<int>(w[2]));
            }
        }
        c.push(it != s->icon_controls.end());
        return 0;
    });
    // 80:1F moves the mouse pointer towards a target over time (6 values: target, time ...); there is no pointer to move here
    add(r, 0x801F, "mouse motion", 6, 0, [](Ctx& c) { for (int i = 0; i < 6; ++i) c.pop(); return 0; });
    // 90:B6 / 90:BA (object, description) and 90:B8 (object): object property lookups, answered "none" until modelled;
    // 90:22 (7 values: priority, skip flag, three timing values, opacity, object): a timed opacity change, not modelled
    add(r, 0x90B6, "paint plain panel buttons", 2, 1, [s](Ctx& c) {
        const Word desc = c.pop(), object = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_DBGPANEL")) std::fprintf(stderr, "90:B6 object %X desc %X groups %d\n", object, desc, int(c.read(desc, 4, false)));
        c.push(panel_paint(*s, c, object, desc, true));
        return 0;
    });
    // 90:B8 (window) / 90:BA (description, number): the plain panel, registered and started like 91:B8 / 91:BA; a number that does
    // not exist gives 1, a panel that has started 4
    add(r, 0x90B8, "register plain panel", 1, 1, [s](Ctx& c) {
        const Word window = c.pop();
        if (!c.ok()) return 0;
        const Word id = ++s->last_icon_control;
        auto& ic = s->icon_controls[id];
        ic = {};
        ic.window = window, ic.basic = true;
        ic.seen_clicks = s->input_counts[1];
        c.push(id);
        return 0;
    });
    add(r, 0x90BA, "start plain panel", 2, 1, [s](Ctx& c) {
        const Word desc = c.pop();
        const Word id = c.pop();
        if (!c.ok()) return 0;
        c.push(panel_start(*s, id, c, desc));
        return 0;
    });
    add(r, 0x8008, "mouse position", 0, 2, [s](Ctx& c) { c.push(static_cast<Word>(s->mouse_x)), c.push(static_cast<Word>(s->mouse_y)); return 0; }); // client x, y
    add(r, 0x810F, "window minimised", 0, 1, [](Ctx& c) { c.push(0); return 0; });
    add(r, 0x8018, "input priority add", 1, 0, [s](Ctx& c) {
        const Word priority = c.pop();
        if (c.ok()) {
            push_input_priority(*s, priority);
            if (std::getenv("OBGI_TRACEPANEL")) std::fprintf(stderr, "INPUT-LAYER add t%u ip=%X priority=%u keytop=%u depth=%zu\n", c.t.id, c.t.ip, priority, s->key_layers.empty() ? 0u : s->key_layers.front(), s->key_layers.size());
        }
        return 0;
    });
    add(r, 0x8019, "input priority remove", 1, 0, [s](Ctx& c) {
        const Word priority = c.pop();
        if (c.ok()) {
            pop_input_priority(*s, priority);
            if (std::getenv("OBGI_TRACEPANEL")) std::fprintf(stderr, "INPUT-LAYER remove t%u ip=%X priority=%u keytop=%u depth=%zu\n", c.t.id, c.t.ip, priority, s->key_layers.empty() ? 0u : s->key_layers.front(), s->key_layers.size());
        }
        return 0;
    });
    // The ruby (furigana) dictionary, word -> reading, longest word first (analysis: the reference engine's gfx/text/ruby.c).
    // 91:94 (word, reading): a null word clears it, a null reading removes the word, otherwise adds / replaces the pair.
    auto ruby_insert = [s](const std::string& word, const std::string& reading) {
        for (auto& e : s->ruby)
            if (e.first == word) { e.second = reading; return; }
        auto pos = s->ruby.begin();
        while (pos != s->ruby.end() && pos->first.size() > word.size()) ++pos;
        s->ruby.insert(pos, {word, reading});
    };
    add(r, 0x9194, "ruby set", 2, 0, [s, ruby_insert](Ctx& c) {
        const Word reading = c.pop(), word = c.pop();
        if (!c.ok()) return 0;
        if (word == 0) { s->ruby.clear(); return 0; }
        const std::string w = c.cstring(word);
        if (reading == 0) { std::erase_if(s->ruby, [&](const auto& e) { return e.first == w; }); return 0; }
        ruby_insert(w, c.cstring(reading));
        return 0;
    });
    // 91:95 (text, tags) -> count: writes "word\reading\n" for every dictionary word that starts at a character of the text (the
    // rest of a matched word is skipped) into the buffer, pushes how many
    add(r, 0x9195, "ruby parse", 2, 1, [s](Ctx& c) {
        const Word tags = c.pop(), str = c.pop();
        if (!c.ok()) return 0;
        const std::string text = c.cstring(str);
        std::string out;
        Word count = 0;
        std::size_t skip = 0;
        const bool utf8 = s->code_page == 65001;
        for (std::size_t i = 0; i < text.size();) {
            const auto b = static_cast<unsigned char>(text[i]);
            const std::size_t len = utf8 ? (b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1) : ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC) ? 2 : 1);
            if (skip == 0) {
                for (const auto& e : s->ruby)
                    if (text.compare(i, e.first.size(), e.first) == 0) {
                        out += e.first + "\\" + e.second + "\n";
                        ++count;
                        std::size_t chars = 0;
                        for (std::size_t k = 0; k < e.first.size(); ++chars) {
                            const auto kb = static_cast<unsigned char>(e.first[k]);
                            k += utf8 ? (kb >= 0xF0 ? 4 : kb >= 0xE0 ? 3 : kb >= 0xC0 ? 2 : 1) : ((kb >= 0x81 && kb <= 0x9F) || (kb >= 0xE0 && kb <= 0xFC) ? 2 : 1);
                        }
                        skip = chars - 1;
                        break;
                    }
            } else
                --skip;
            i += len;
        }
        if (out.size() >= 0x400) out.resize(0x3FF);
        c.write_cstring(tags, out);
        c.push(count);
        return 0;
    });
    // 91:96 (list): adds the "word\reading" lines of the list to the dictionary; 1 when the whole list was consumed
    add(r, 0x9196, "ruby add list", 1, 1, [s, ruby_insert](Ctx& c) {
        const Word list = c.pop();
        if (!c.ok()) return 0;
        const std::string text = list ? c.cstring(list, 0x10000) : std::string();
        std::size_t p = 0;
        bool whole = list != 0;
        while (p < text.size()) {
            const std::size_t sep = text.find('\\', p);
            if (sep == std::string::npos || sep == p) { whole = false; break; }
            const std::string word = text.substr(p, sep - p);
            p = sep + 1;
            const std::size_t end = text.find('\n', p);
            const std::size_t len = (end == std::string::npos ? text.size() : end) - p;
            if (len > 0) ruby_insert(word, text.substr(p, len));
            p += len + (end == std::string::npos ? 0 : 1);
        }
        c.push(whole ? 1 : 0);
        return 0;
    });

    // icon controls (DCIPIconEx): 91:B8 (window) registers one and pushes its number; 91:BA (description, number)
    // pushes 1 when the number does not exist; 90:B9 (number) removes it and pushes 1, or 0 when not found
    add(r, 0x91B8, "register icon control", 1, 1, [s](Ctx& c) {
        const Word window = c.pop();
        if (!c.ok()) return 0;
        const Word id = ++s->last_icon_control;
        s->icon_controls[id] = {};
        s->icon_controls[id].window = window;
        s->icon_controls[id].seen_clicks = s->input_counts[1]; // clicks before the panel existed are not its clicks
        c.push(id);
        return 0;
    });
    add(r, 0x91BA, "icon control description", 2, 1, [s](Ctx& c) {
        const Word desc = c.pop(); // description address (popped first)
        const Word id = c.pop();
        if (!c.ok()) return 0;
        c.push(panel_start(*s, id, c, desc));
        return 0;
    });
    add(r, 0x90B9, "remove icon control", 1, 1, [s](Ctx& c) {
        const Word id = c.pop();
        if (!c.ok()) return 0;
        if (std::getenv("OBGI_TRACEPANEL") && id <= 32) {
            const auto it = s->icon_controls.find(id);
            std::fprintf(stderr, "PANEL remove t%u ip=%X control=%u exists=%d window=%u pending=%zu\n",
                         c.t.id, c.t.ip, id, it != s->icon_controls.end(),
                         it == s->icon_controls.end() ? 0u : it->second.window,
                         it == s->icon_controls.end() ? 0u : it->second.events.size());
        }
        panel_stop(*s, id);
        c.push(s->icon_controls.erase(id) ? 1 : 0);
        return 0;
    });
    add_setter(r, state, 0x8068, "window close policy");
    add_setter(r, state, 0x8052, "setting 80:52");
    add_setter(r, state, 0x8074, "setting 80:74");
    add_setter(r, state, 0x8036, "setting 80:36");
    add_setter(r, state, 0x8067, "setting 80:67", 4);
    add_setter(r, state, 0x9100, "setting 91:00");
    add_setter(r, state, 0x910D, "setting 91:0D");
    add_setter(r, state, 0x909F, "setting 90:9F");
    add_setter(r, state, 0x900D, "display mode", 3);
    add_setter(r, state, 0x9003, "display object cache size", 0x20000000);
    add_setter(r, state, 0x90AF, "display option 90:AF");
    // (a, b): two numbers kept by the display object manager, which then refreshes (nothing script-visible)
    add(r, 0x9006, "display object pair", 2, 0, [s](Ctx& c) {
        const Word a = c.pop(), b = c.pop();
        if (c.ok()) {
            s->values[0x9006] = b;
            s->values[0x9007] = a; // stored under a made-up neighbour id; no op 90:07 writes it
        }
        return 0;
    });
    add(r, 0x9008, "refresh display objects", 1, 0, [](Ctx& c) {
        c.pop();
        return 0;
    });
    add_flag(r, state, 0x80AF, "flag 80:AF");
    add_flag(r, state, 0x9109, "flag 91:09");
    add_flag(r, state, 0x8162, "flag 81:62");
}

} // namespace obgi
