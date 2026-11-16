#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

#include "resources/archive_store.hpp"

namespace obgi {

// Identify known translated script contents, not a game title or directory name.
inline bool has_gbk_story_patch(ArcResourceFinder& resources) {
    const auto contains = [](const auto& script, const auto& bytes) {
        return std::search(script.begin(), script.end(), bytes.begin(), bytes.end()) != script.end();
    };
    auto script = resources.read("data01120.arc", "ac3_01mikuri");
    if (script && script->size() >= 0x120 &&
        std::string_view(reinterpret_cast<const char*>(script->data()), script->size()).starts_with("BurikoCompiledScriptVer1.00")) {
        constexpr std::array<std::uint8_t, 6> name = {0xC3, 0xC0, 0xBE, 0xC3, 0xC0, 0xF5};
        constexpr std::array<std::uint8_t, 10> phrase = {0xBD, 0xF1, 0xCC, 0xEC, 0xCA, 0xC7, 0xC8, 0xA5, 0xBB, 0xA8};
        if (contains(*script, name) && contains(*script, phrase)) return true;
    }
    script = resources.read_archive("data01000.arc", "aiy00010");
    if (!script || script->size() < 0x120 ||
        !std::string_view(reinterpret_cast<const char*>(script->data()), script->size()).starts_with("BurikoCompiledScriptVer1.00"))
        return false;
    // Two GBK passages from the Eustia translation; the original Japanese script lacks them.
    constexpr std::array<std::uint8_t, 16> first = {0xc8,0xcb,0xc9,0xfa,0xb2,0xbb,0xc8,0xe7,0xd2,0xe2,0xca,0xc2,0xa3,0xac,0xca,0xae};
    constexpr std::array<std::uint8_t, 16> second = {0xca,0xc0,0xbc,0xe4,0xb5,0xc4,0xcd,0xf2,0xce,0xef,0xbd,0xd4,0xb1,0xbb,0xd7,0xb0};
    return contains(*script, first) && contains(*script, second);
}

} // namespace obgi
