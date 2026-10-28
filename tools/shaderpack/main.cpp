// Build-time tool: obgi_shaderpack <out.pack> <name:format:entry:path>...
// format is one of spirv, dxbc, dxil, msl, metallib. Entries naming the same path share one blob.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include "render/shader_pack.hpp"

namespace {
using namespace obgi::shader_pack;

struct Entry {
    std::string name, entry;
    std::uint32_t format = 0;
    std::string path;
};

void put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
}

void put_fixed(std::vector<std::uint8_t>& out, const std::string& s, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) out.push_back(i < s.size() ? static_cast<std::uint8_t>(s[i]) : 0);
}
} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: obgi_shaderpack <out.pack> <name:format:entry:path>...\n");
        return 2;
    }
    const std::map<std::string, std::uint32_t> formats = {
        {"spirv", kSpirv}, {"dxbc", kDxbc}, {"dxil", kDxil}, {"msl", kMsl}, {"metallib", kMetallib}};
    std::vector<Entry> entries;
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        // path may contain ':' (drive letters), so only the first three separators split the argument
        std::size_t p1 = a.find(':'), p2 = a.find(':', p1 + 1), p3 = a.find(':', p2 + 1);
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos) {
            std::fprintf(stderr, "bad entry: %s\n", argv[i]);
            return 2;
        }
        Entry e;
        e.name = a.substr(0, p1);
        auto it = formats.find(a.substr(p1 + 1, p2 - p1 - 1));
        if (it == formats.end() || e.name.size() >= kNameSize) {
            std::fprintf(stderr, "bad entry: %s\n", argv[i]);
            return 2;
        }
        e.format = it->second;
        e.entry = a.substr(p2 + 1, p3 - p2 - 1);
        if (e.entry.size() >= kEntryPointSize) return 2;
        e.path = a.substr(p3 + 1);
        entries.push_back(std::move(e));
    }

    std::vector<std::uint8_t> blobs;
    std::map<std::string, std::pair<std::uint32_t, std::uint32_t>> placed; // path -> offset, size
    const std::uint32_t dir_end = static_cast<std::uint32_t>(kHeaderSize + entries.size() * kEntrySize);
    for (const auto& e : entries) {
        if (placed.count(e.path)) continue;
        std::ifstream f(e.path, std::ios::binary);
        if (!f) {
            std::fprintf(stderr, "cannot read %s\n", e.path.c_str());
            return 1;
        }
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        while ((dir_end + blobs.size()) % 16) blobs.push_back(0);
        placed[e.path] = {static_cast<std::uint32_t>(dir_end + blobs.size()), static_cast<std::uint32_t>(bytes.size())};
        blobs.insert(blobs.end(), bytes.begin(), bytes.end());
    }

    std::vector<std::uint8_t> out(kMagic, kMagic + 4);
    put32(out, kVersion);
    put32(out, static_cast<std::uint32_t>(entries.size()));
    put32(out, 0);
    for (const auto& e : entries) {
        put_fixed(out, e.name, kNameSize);
        put_fixed(out, e.entry, kEntryPointSize);
        put32(out, e.format);
        put32(out, placed[e.path].first);
        put32(out, placed[e.path].second);
        put32(out, 0);
    }
    out.insert(out.end(), blobs.begin(), blobs.end());
    std::ofstream o(argv[1], std::ios::binary);
    o.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return o ? 0 : 1;
}
