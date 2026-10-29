#include "bindings/system_subops.hpp"

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iterator>
#include <optional>
#include <system_error>

#include "formats/sdc.hpp"
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
    std::u32string wide = cp932_to_utf32(script_path);
    std::string utf8;
    for (char32_t ch : wide) {
        if (ch == U'\\') ch = U'/';
        if (ch < 0x80) utf8 += char(ch);
        else if (ch < 0x800) utf8 += {char(0xC0 | (ch >> 6)), char(0x80 | (ch & 0x3F))};
        else if (ch < 0x10000) utf8 += {char(0xE0 | (ch >> 12)), char(0x80 | ((ch >> 6) & 0x3F)), char(0x80 | (ch & 0x3F))};
        else utf8 += {char(0xF0 | (ch >> 18)), char(0x80 | ((ch >> 12) & 0x3F)), char(0x80 | ((ch >> 6) & 0x3F)), char(0x80 | (ch & 0x3F))};
    }
    std::filesystem::path p(std::u8string(utf8.begin(), utf8.end()));
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
    add(r, 0x90B7, "object method (not modelled)", 2, 1, [](Ctx& c) { c.pop(), c.pop(); c.push(0); return 0; });
    add(r, 0x90E0, "create manager object", 0, 1, [s](Ctx& c) { c.push(0xA0000000u + s->last_manager_object++); return 0; });
    add(r, 0x90E1, "90:E1 (not modelled)", 1, 0, [](Ctx& c) { c.pop(); return 0; });
    add(r, 0x90E4, "90:E4 (not modelled)", 2, 0, [](Ctx& c) { c.pop(), c.pop(); return 0; });
    add(r, 0x90E5, "90:E5 (not modelled)", 4, 0, [](Ctx& c) { for (int i = 0; i < 4; ++i) c.pop(); return 0; });
    add(r, 0x90E8, "90:E8 (not modelled)", 4, 0, [](Ctx& c) { for (int i = 0; i < 4; ++i) c.pop(); return 0; });
    add(r, 0x90E9, "90:E9 (not modelled)", 2, 0, [](Ctx& c) { c.pop(), c.pop(); return 0; });
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
    add(r, 0x800C, "local time", 1, 0, [](Ctx& c) {
        const Word dst = c.pop();
        if (!c.ok()) return 0;
        const std::time_t now = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &now);
#else
        localtime_r(&now, &tm);
#endif
        const std::uint16_t f[8] = {std::uint16_t(tm.tm_year + 1900), std::uint16_t(tm.tm_mon + 1), std::uint16_t(tm.tm_wday),
                                    std::uint16_t(tm.tm_mday), std::uint16_t(tm.tm_hour), std::uint16_t(tm.tm_min),
                                    std::uint16_t(tm.tm_sec), 0};
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
    add(r, 0x8034, "resource exists", 2, 1, [s, res](Ctx& c) {
        std::string name = c.cstring(c.pop());
        std::string prefix = c.cstring(c.pop());
        if (!c.ok()) return 0;
        std::error_code ec;
        bool found = false;
        if (std::getenv("OBGI_DBG34")) std::fprintf(stderr, "80:34 prefix=[%s] name=[%s]\n", prefix.c_str(), name.c_str());
        if (!name.empty()) {
            const std::filesystem::path as_given = host_path(*s, name);
            found = std::filesystem::is_regular_file(as_given, ec) || std::filesystem::is_regular_file(std::filesystem::path(name), ec);
            if (!found && std::filesystem::path(name).is_relative()) found = std::filesystem::is_regular_file(s->game_dir / name, ec);
        }
        if (!found) found = res->exists(prefix, name);
        c.push(found ? 1 : 0);
        return 0;
    });
    // (destination, SDC block): unpacks an SDC block that holds a "DCFS FORMAT 1.00" container and expands it at the
    // destination: a base plane of N bytes, then M-1 planes of N bytes, each given as alternating runs (a varint length;
    // "same as the previous plane", then "literal", ...). Pushes M (the number of planes), 0 for a block that is not
    // SDC or not DCFS.
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
    add(r, 0x8033, "delete file", 2, 1, [s](Ctx& c) {
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
    add(r, 0x8130, "load resource bytes", 5, 1, [load_resource](Ctx& c) {
        const Word size = c.pop(), offset = c.pop();
        std::string name = c.cstring(c.pop());
        const Word archive_addr = c.pop();
        std::string archive = archive_addr ? c.cstring(archive_addr) : std::string();
        const Word dst = c.pop();
        if (!c.ok()) return 0;
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
    add(r, 0x8028, "create directory", 1, 1, [s](Ctx& c) {
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
    add(r, 0x8081, "save global data", 0, 1, [s](Ctx& c) {
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
    add(r, 0x8037, "add resource search path", 1, 0, [s](Ctx& c) {
        std::string path = c.cstring(c.pop());
        if (c.ok()) s->search_paths.insert(s->search_paths.begin(), std::move(path));
        return 0;
    });
    // (a, b, index): remembers two numbers per index below 16. 1 = bad index, 2 = a zero number, 0 = stored
    add(r, 0x8160, "pair table", 3, 1, [s](Ctx& c) {
        const Word a = c.pop(), b = c.pop(), idx = c.pop();
        if (!c.ok()) return 0;
        if (idx >= 16) c.push(1);
        else if (a == 0 || b == 0) c.push(2);
        else {
            s->pair_table[idx] = {a, b};
            c.push(0);
        }
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
        s->frame_ms = v ? 1000 / v : 0;
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
    // No window, no keys: nothing is pressed, the window is active and not minimised.
    add(r, 0x8017, "input skip state", 0, 1, [](Ctx& c) { c.push(0); return 0; }); // nothing pressed
    add(r, 0x8014, "set input skip permission", 1, 0, [s](Ctx& c) { const Word v = c.pop(); if (c.ok()) s->values[0x8014] = v; return 0; });
    add(r, 0x800F, "window active", 0, 1, [](Ctx& c) { c.push(1); return 0; });
    // 92:15 only calls a flush routine in the original (no arguments, no result); 90:43 / 90:28 take 9 / 12 values for
    // grey-bitmap effects and a motion wait object: not modelled yet
    add(r, 0x9215, "flush 92:15", 0, 0, [](Ctx&) { return 0; });
    add(r, 0x9043, "grey bitmap effect (not modelled)", 9, 0, [](Ctx& c) { for (int i = 0; i < 9; ++i) c.pop(); return 0; });
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
            for (int k = 0; k < 3; ++k) c.write(addr + 4 * k, ev[k], 4);
        }
        c.push(it != s->icon_controls.end());
        return 0;
    });
    // 81:35 (two path strings): a file operation on the user's disk (FUN_00475b50); its pushed value is whatever register
    // was left over in the original, so scripts cannot depend on it. Not modelled: pops both and pushes 0.
    add(r, 0x8135, "file operation (not modelled)", 2, 1, [](Ctx& c) { c.pop(), c.pop(); c.push(0); return 0; });
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
            if (w[0] == 0x10000000) {
                it->second.events.push_back({0x10000006u, w[1], w[2]});
                it->second.info = {1, 0, w[1], 0, 0, 0};
            }
        }
        c.push(it != s->icon_controls.end());
        return 0;
    });
    // 80:1F moves the mouse pointer towards a target over time (6 values: target, time ...); there is no pointer to move here
    add(r, 0x801F, "mouse motion", 6, 0, [](Ctx& c) { for (int i = 0; i < 6; ++i) c.pop(); return 0; });
    // 90:B6 / 90:BA (object, description) and 90:B8 (object): object property lookups, answered "none" until modelled;
    // 90:22 (7 values: priority, skip flag, three timing values, opacity, object): a timed opacity change, not modelled
    add(r, 0x90B6, "object property (not modelled)", 2, 1, [](Ctx& c) { c.pop(), c.pop(); c.push(0); return 0; });
    add(r, 0x90BA, "object property (not modelled)", 2, 1, [](Ctx& c) { c.pop(), c.pop(); c.push(0); return 0; });
    add(r, 0x90B8, "object property (not modelled)", 1, 1, [](Ctx& c) { c.pop(); c.push(0); return 0; });
    add(r, 0x9022, "timed opacity (not modelled)", 7, 0, [](Ctx& c) { for (int i = 0; i < 7; ++i) c.pop(); return 0; });
    add(r, 0x8008, "mouse position", 0, 2, [](Ctx& c) { c.push(0), c.push(0); return 0; }); // client x, y; no pointer here
    add(r, 0x810F, "window minimised", 0, 1, [](Ctx& c) { c.push(0); return 0; });
    add(r, 0x8018, "input priority add", 1, 0, [](Ctx& c) { c.pop(); return 0; });
    add(r, 0x8019, "input priority remove", 1, 0, [](Ctx& c) { c.pop(); return 0; });
    // (definitions): text replacement list, "original\replacement" per line; always succeeds
    add(r, 0x9196, "text replacement list", 1, 1, [s](Ctx& c) {
        s->replacements = c.cstring(c.pop());
        if (!c.ok()) return 0;
        c.push(1);
        return 0;
    });

    // icon controls (DCIPIconEx): 91:B8 (window) registers one and pushes its number; 91:BA (description, number)
    // pushes 1 when the number does not exist; 90:B9 (number) removes it and pushes 1, or 0 when not found
    add(r, 0x91B8, "register icon control", 1, 1, [s](Ctx& c) {
        c.pop();
        if (!c.ok()) return 0;
        const Word id = ++s->last_icon_control;
        s->icon_controls[id] = {};
        c.push(id);
        return 0;
    });
    add(r, 0x91BA, "icon control description", 2, 1, [s](Ctx& c) {
        c.pop(); // description address (popped first)
        const Word id = c.pop();
        if (!c.ok()) return 0;
        c.push(s->icon_controls.count(id) ? 0 : 1);
        return 0;
    });
    add(r, 0x90B9, "remove icon control", 1, 1, [s](Ctx& c) {
        const Word id = c.pop();
        if (!c.ok()) return 0;
        c.push(s->icon_controls.erase(id) ? 1 : 0);
        return 0;
    });
    // 90:05 (upper, lower display priority bounds, each at most 0xFFFF); 90:20 (priority, B, frame rate, D, blend, object)
    add(r, 0x9005, "display priority range", 2, 0, [](Ctx& c) {
        const Word lo = c.pop(), hi = c.pop();
        if (c.ok() && (lo > 0xFFFF || hi > 0xFFFF)) c.fail(Errc::Range, "display priority range out of bounds");
        return 0;
    });
    add(r, 0x9020, "object motion wait", 6, 0, [](Ctx& c) { for (int i = 0; i < 6; ++i) c.pop(); return 0; });

    add_setter(r, state, 0x8068, "window close policy");
    add_setter(r, state, 0x8052, "setting 80:52");
    add_setter(r, state, 0x8074, "setting 80:74");
    add_setter(r, state, 0x8036, "setting 80:36");
    add_setter(r, state, 0x8067, "setting 80:67", 4);
    add_setter(r, state, 0x9100, "setting 91:00");
    add_setter(r, state, 0x910D, "setting 91:0D");
    add_setter(r, state, 0x909F, "setting 90:9F");
    add_setter(r, state, 0x900D, "display mode", 3);
    add_setter(r, state, 0xC01F, "setting C0:1F", 2);
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
