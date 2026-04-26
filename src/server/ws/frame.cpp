#include "ws/frame.h"

#include <format>
#include <iostream>

namespace ws {

namespace {
// Cap on a single frame's payload size. Far above anything signaling
// produces; guards against bad clients asking us to allocate gigabytes.
constexpr uint64_t kMaxFramePayload = 1 << 20; // 1 MiB
} // namespace

ParseResult ParseFrame(std::string& buf, Frame& out) {
    // Need at least the 2-byte minimum header.
    if (buf.size() < 2)
        return ParseResult::NeedMore;

    auto* p = reinterpret_cast<const uint8_t*>(buf.data());

    bool fin = (p[0] & 0x80) != 0;
    uint8_t rsv = p[0] & 0x70; // RSV1|RSV2|RSV3
    uint8_t opcode = p[0] & 0x0F;

    // No extensions negotiated, so all RSV bits MUST be zero.
    if (rsv != 0)
        return ParseResult::Bad;

    bool masked = (p[1] & 0x80) != 0;
    // Per RFC 6455 §5.3 a client-to-server frame MUST be masked.
    if (!masked)
        return ParseResult::Bad;

    uint64_t payload_len = p[1] & 0x7F;
    size_t header_len = 2;

    if (payload_len == 126) {
        if (buf.size() < 4)
            return ParseResult::NeedMore;
        payload_len = (uint64_t(p[2]) << 8) | uint64_t(p[3]);
        header_len = 4;
    } else if (payload_len == 127) {
        if (buf.size() < 10)
            return ParseResult::NeedMore;
        payload_len = 0;
        for (int i = 0; i < 8; ++i) {
            payload_len = (payload_len << 8) | uint64_t(p[2 + i]);
        }
        header_len = 10;
    }

    if (payload_len > kMaxFramePayload) {
        std::cerr << std::format(
            "Bad payload... payload len ({}) exceeded max payload len ({})\n",
            payload_len, kMaxFramePayload);
        return ParseResult::Bad;
    }

    // Header + 4-byte mask key + payload must all be present.
    size_t total = header_len + 4 + size_t(payload_len);
    if (buf.size() < total)
        return ParseResult::NeedMore;

    const uint8_t* mask_key = p + header_len;
    const uint8_t* payload_src = p + header_len + 4;

    out.fin = fin;
    out.opcode = static_cast<Op>(opcode);
    out.payload.resize(payload_len);
    for (uint64_t i = 0; i < payload_len; ++i) {
        out.payload[i] = static_cast<char>(payload_src[i] ^ mask_key[i % 4]);
    }

    buf.erase(0, total);
    return ParseResult::Got;
}

std::string EncodeFrame(Op opcode, std::string_view payload) {
    std::string frame;
    frame.reserve(payload.size() + 10);

    // Byte 0: FIN=1, RSV=0, opcode.
    frame.push_back(static_cast<char>(0x80 | static_cast<uint8_t>(opcode)));

    // Length (server-originated frames are never masked, so MASK bit = 0).
    uint64_t len = payload.size();
    if (len < 126) {
        frame.push_back(static_cast<char>(len));
    } else if (len < 65536) {
        frame.push_back(static_cast<char>(126));
        frame.push_back(static_cast<char>((len >> 8) & 0xFF));
        frame.push_back(static_cast<char>(len & 0xFF));
    } else {
        frame.push_back(static_cast<char>(127));
        for (int i = 7; i >= 0; --i) {
            frame.push_back(static_cast<char>((len >> (i * 8)) & 0xFF));
        }
    }

    frame.append(payload);
    return frame;
}

} // namespace ws
