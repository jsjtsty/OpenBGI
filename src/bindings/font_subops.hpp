#pragma once
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "text/font.hpp"
#include "vm/vm.hpp"

namespace obgi {
class ArcResourceFinder;

// The engine's font tables, filled by the scripts at start-up (analysis/子opcode语义表.md, B0:C1..C7, 91:0E).
struct FontTable {
    std::vector<std::string> ids; // B0:C1: font names in registration order; the index is the id scripts get back
    struct Info {
        std::string name;
        std::optional<std::string> alias; // B0:C7
        int charset = -1;                 // Windows charset set by B0:C1 (0x80 Shift-JIS, 0 ANSI, 0x86 GB2312, 0x88 Big5)
    };
    std::vector<Info> infos;
    std::optional<std::string> default_alias; // B0:C7 with no font name
    struct Definition {                       // 91:0E: scale and offset of a font, 16.16 fixed point
        std::uint32_t scale_x = 0, scale_y = 0;
        std::int32_t offset_x = 0, offset_y = 0;
    };
    std::map<std::string, Definition> definitions;
};

struct FontHost {
    FontLibrary* fonts = nullptr;
    FontTable table;
};

// B0:C1 register a font name, B0:C5 list installed fonts, B0:C6 look a font up, B0:C7 set an alias, 91:0E define
// a font's scale. "Installed" means the engine can load a file for that exact name (see FontLibrary::has_exact).
void register_font_subops(vm::SubopRegistry& registry, FontHost& host, ArcResourceFinder* resources = nullptr);

} // namespace obgi
