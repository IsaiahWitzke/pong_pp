#include "util/sha1.h"

#include <cstdint>
#include <cstring>

namespace util {

namespace {

inline uint32_t leftrotate(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

void process_block(const uint8_t* block, uint32_t h[5]) {
    uint32_t w[80];
    // Load the block as 16 big-endian 32-bit words.
    for (int i = 0; i < 16; ++i) {
        w[i] = (uint32_t(block[i * 4]) << 24) |
               (uint32_t(block[i * 4 + 1]) << 16) |
               (uint32_t(block[i * 4 + 2]) << 8) | uint32_t(block[i * 4 + 3]);
    }
    // Extend to 80 words.
    for (int i = 16; i < 80; ++i) {
        w[i] = leftrotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

    for (int i = 0; i < 80; ++i) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t temp = leftrotate(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = leftrotate(b, 30);
        b = a;
        a = temp;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

} // namespace

std::string sha1(std::string_view data) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};

    // Process every complete 64-byte block of the input directly.
    size_t pos = 0;
    while (pos + 64 <= data.size()) {
        process_block(reinterpret_cast<const uint8_t*>(data.data() + pos), h);
        pos += 64;
    }

    // Build the final block(s): remaining bytes, 0x80, zeros, then the
    // 64-bit big-endian total bit length. May span 1 or 2 blocks depending
    // on how many tail bytes are left.
    uint8_t tail[128] = {};
    size_t rem = data.size() - pos;
    std::memcpy(tail, data.data() + pos, rem);
    tail[rem] = 0x80;

    size_t blocks = (rem + 1 + 8 <= 64) ? 1 : 2;
    size_t final_size = blocks * 64;

    uint64_t total_bits = uint64_t(data.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        tail[final_size - 1 - i] = uint8_t(total_bits >> (i * 8));
    }

    process_block(tail, h);
    if (blocks == 2) {
        process_block(tail + 64, h);
    }

    // Output 20 raw bytes, big-endian per word.
    std::string out(20, '\0');
    for (int i = 0; i < 5; ++i) {
        out[i * 4]     = char(uint8_t(h[i] >> 24));
        out[i * 4 + 1] = char(uint8_t(h[i] >> 16));
        out[i * 4 + 2] = char(uint8_t(h[i] >> 8));
        out[i * 4 + 3] = char(uint8_t(h[i]));
    }
    return out;
}

} // namespace util
