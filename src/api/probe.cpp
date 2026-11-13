#include "openbgi/openbgi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "bindings/arc_program_provider.hpp"
#include "core/bytes.hpp"
#include "resources/archive_store.hpp"
#include "vfs/arc.hpp"

#ifndef OPENBGI_VERSION
#define OPENBGI_VERSION "0.1.0"
#endif

namespace {

namespace fs = std::filesystem;

void copy_out(const std::string& src, char* dst, size_t size) {
    if (!dst || size == 0) return;
    std::snprintf(dst, size, "%s", src.c_str());
}

fs::path path_from_utf8(const char* s) {
    if (!s) return {};
    const std::string_view sv(s);
    return fs::path(std::u8string(sv.begin(), sv.end()));
}

std::string sanitize_id(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (unsigned char c : name) {
        if (std::isalnum(c)) out.push_back(static_cast<char>(std::tolower(c)));
        else if ((c == '_' || c == '-') && !out.empty() && out.back() != '_') out.push_back('_');
    }
    if (out.empty()) out = "bgi_game";
    return out;
}

std::optional<std::string> game_name_from_system_ipl(obgi::vm::ProgramProvider& programs) {
    auto block = programs.load("system.arc", "ipl._bp");
    if (!block || block->size() < 8) return std::nullopt;
    const std::uint32_t offset = obgi::load_u32le(block->data());
    const std::uint32_t size = obgi::load_u32le(block->data() + 4);
    if (static_cast<std::uint64_t>(offset) + size > block->size()) return std::nullopt;
    const std::string_view code(reinterpret_cast<const char*>(block->data() + offset), size);
    constexpr std::string_view marker("debug.inf\0", 10);
    const auto marker_at = code.find(marker);
    if (marker_at == std::string_view::npos) return std::nullopt;
    const std::size_t begin = marker_at + marker.size();
    const auto end = code.find('\0', begin);
    if (end == std::string_view::npos || end == begin || end - begin > 128) return std::nullopt;
    const std::string_view name = code.substr(begin, end - begin);
    if (!std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isalnum(ch) != 0; }))
        return std::nullopt;
    return std::string(name);
}

bool has_gbk_story_patch(obgi::ArcResourceFinder& resources) {
    auto script = resources.read("data01120.arc", "ac3_01mikuri");
    if (!script || script->size() < 0x120 ||
        !std::string_view(reinterpret_cast<const char*>(script->data()), script->size()).starts_with("BurikoCompiledScriptVer1.00"))
        return false;
    constexpr std::array<std::uint8_t, 6> name = {0xC3, 0xC0, 0xBE, 0xC3, 0xC0, 0xF5}; // GBK: 美久栗
    constexpr std::array<std::uint8_t, 10> phrase_prefix = {0xBD, 0xF1, 0xCC, 0xEC, 0xCA, 0xC7, 0xC8, 0xA5, 0xBB, 0xA8};
    const auto contains = [&](const auto& bytes) {
        return std::search(script->begin(), script->end(), bytes.begin(), bytes.end()) != script->end();
    };
    return contains(name) && contains(phrase_prefix);
}

std::map<std::string, fs::path> scan_arcs(const fs::path& dir) {
    std::map<std::string, fs::path> arcs;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        std::string lower = obgi::ascii_lower(entry.path().filename().string());
        if (lower.ends_with(".arc")) arcs.emplace(std::move(lower), entry.path());
    }
    return arcs;
}

std::string folder_display_name(const fs::path& dir) {
    const auto u8 = dir.filename().u8string();
    std::string s(reinterpret_cast<const char*>(u8.data()), u8.size());
    return s.empty() ? "OpenBGI Game" : s;
}

} // namespace

extern "C" {

int obgi_detect(const char* dir, obgi_game_info* info) {
    if (!dir || !*dir) return 0;
    const fs::path root = path_from_utf8(dir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) return 0;

    const auto arcs = scan_arcs(root);
    if (!arcs.contains("autoload.arc") && !arcs.contains("system.arc")) return 0;

    std::string display = folder_display_name(root);
    std::string id = sanitize_id(display);

    if (arcs.contains("system.arc")) {
        obgi::ArcResourceFinder resources(root);
        obgi::ArcProgramProvider programs(resources);
        if (auto detected = game_name_from_system_ipl(programs)) {
            display = *detected;
            id = sanitize_id(*detected);
        }
    }

    if (info) {
        std::memset(info, 0, sizeof(*info));
        copy_out(id, info->id, sizeof(info->id));
        copy_out(display, info->name, sizeof(info->name));
    }
    return 1;
}

int obgi_probe(const char* game_dir, obgi_probe_result* result) {
    if (!result) return 0;
    std::memset(result, 0, sizeof(*result));
    if (!game_dir || !*game_dir) {
        copy_out("game directory path is empty", result->message, sizeof(result->message));
        return 0;
    }

    const fs::path root = path_from_utf8(game_dir);
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        copy_out("game directory does not exist", result->message, sizeof(result->message));
        return 0;
    }

    const auto arcs = scan_arcs(root);
    result->arcs_found = static_cast<int>(arcs.size());

    std::string display = folder_display_name(root);
    std::string id = sanitize_id(display);

    std::vector<std::string> problems;
    bool boot_ok = true;

    for (const char* req : {"autoload.arc", "system.arc"}) {
        auto it = arcs.find(req);
        if (it == arcs.end()) {
            boot_ok = false;
            problems.emplace_back(req);
            continue;
        }
        auto opened = obgi::Arc::open(it->second);
        if (!opened) {
            boot_ok = false;
            problems.emplace_back(req);
        } else if (std::string_view(req) == "autoload.arc" && !opened->find("ipl._bp")) {
            boot_ok = false;
            problems.emplace_back("autoload.arc(ipl._bp)");
        }
    }

    bool all_ok = boot_ok;
    for (const auto& [name, path] : arcs) {
        if (name == "autoload.arc" || name == "system.arc") continue;
        if (auto opened = obgi::Arc::open(path); !opened) {
            all_ok = false;
            problems.push_back(name);
        }
    }

    if (arcs.contains("system.arc")) {
        obgi::ArcResourceFinder resources(root);
        obgi::ArcProgramProvider programs(resources);
        if (auto detected = game_name_from_system_ipl(programs)) {
            display = *detected;
            id = sanitize_id(*detected);
        }
        if (has_gbk_story_patch(resources)) result->has_sc_patch = 1;
    }

    std::string missing_csv;
    for (std::size_t i = 0; i < problems.size(); ++i) {
        if (i > 0) missing_csv += ", ";
        missing_csv += problems[i];
    }

    result->ok = boot_ok ? 1 : 0;
    result->complete = (boot_ok && all_ok) ? 1 : 0;
    copy_out(id, result->game_id, sizeof(result->game_id));
    copy_out(display, result->game_name, sizeof(result->game_name));
    copy_out(missing_csv, result->missing, sizeof(result->missing));
    if (!boot_ok) {
        copy_out("missing or unreadable required archives: " + missing_csv, result->message, sizeof(result->message));
    } else if (!all_ok) {
        copy_out("some archives failed to open: " + missing_csv, result->message, sizeof(result->message));
    }
    return result->ok;
}

const char* obgi_version(void) {
    return OPENBGI_VERSION;
}

} // extern "C"
