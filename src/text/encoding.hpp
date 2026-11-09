#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace obgi {

// Script strings are Windows code page 932 (Shift-JIS). Undecodable bytes become U+FFFD.
std::u32string cp932_to_utf32(std::string_view bytes);
std::u32string cp936_to_utf32(std::string_view bytes);
std::u32string utf8_to_utf32(std::string_view bytes);
std::u32string script_to_utf32(std::string_view bytes, std::uint32_t code_page);

} // namespace obgi
