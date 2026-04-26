#include "base64.h"

#include <cstdint>

namespace {
constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                             "abcdefghijklmnopqrstuvwxyz"
                             "0123456789+/";
} // namespace

std::string base64_encode(std::string_view data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);

    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t triple = (uint32_t(uint8_t(data[i])) << 16) |
                          (uint32_t(uint8_t(data[i + 1])) << 8) |
                          uint32_t(uint8_t(data[i + 2]));
        out += kAlphabet[(triple >> 18) & 0x3F];
        out += kAlphabet[(triple >> 12) & 0x3F];
        out += kAlphabet[(triple >> 6) & 0x3F];
        out += kAlphabet[triple & 0x3F];
        i += 3;
    }

    size_t rem = data.size() - i;
    if (rem == 1) {
        uint32_t v = uint32_t(uint8_t(data[i])) << 16;
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += '=';
        out += '=';
    } else if (rem == 2) {
        uint32_t v = (uint32_t(uint8_t(data[i])) << 16) |
                     (uint32_t(uint8_t(data[i + 1])) << 8);
        out += kAlphabet[(v >> 18) & 0x3F];
        out += kAlphabet[(v >> 12) & 0x3F];
        out += kAlphabet[(v >> 6) & 0x3F];
        out += '=';
    }

    return out;
}
