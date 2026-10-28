#include "text/encoding.hpp"

namespace obgi {

namespace {
#include "text/cp932_table.inc"
}

std::u32string cp932_to_utf32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            out.push_back(b);
            ++i;
        } else if (b >= 0xA1 && b <= 0xDF) { // half-width katakana
            out.push_back(0xFF61 + (b - 0xA1));
            ++i;
        } else if ((b >= 0x81 && b <= 0x9F) || (b >= 0xE0 && b <= 0xFC)) {
            const unsigned row = b <= 0x9F ? b - 0x81 : b - 0xE0 + 0x1F;
            char32_t cp = 0xFFFD;
            if (i + 1 < s.size()) {
                const auto t = static_cast<unsigned char>(s[i + 1]);
                if (t >= 0x40 && t <= 0xFC && kCp932Table[row][t - 0x40]) cp = kCp932Table[row][t - 0x40];
            }
            out.push_back(cp);
            i += 2;
        } else { // 0x80, 0xA0, 0xFD..0xFF
            out.push_back(b == 0x80 ? 0x80 : b == 0xA0 ? 0xA0 : 0xFFFD);
            ++i;
        }
    }
    return out;
}

std::u32string utf8_to_utf32(std::string_view s) {
    std::u32string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        int extra = b < 0x80 ? 0 : (b >> 5) == 6 ? 1 : (b >> 4) == 14 ? 2 : (b >> 3) == 30 ? 3 : -1;
        if (extra < 0 || i + extra >= s.size() + (extra == 0 ? 1 : 0)) {
            out.push_back(0xFFFD);
            ++i;
            continue;
        }
        char32_t cp = extra == 0 ? b : b & (0x3F >> extra);
        bool good = true;
        for (int k = 1; k <= extra; ++k) {
            const auto c = static_cast<unsigned char>(s[i + k]);
            if ((c & 0xC0) != 0x80) { good = false; break; }
            cp = cp << 6 | (c & 0x3F);
        }
        out.push_back(good ? cp : 0xFFFD);
        i += good ? extra + 1 : 1;
    }
    return out;
}

} // namespace obgi
