#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace ws {

enum class Op : uint8_t {
    Continuation = 0x0,
    Text = 0x1,
    Binary = 0x2,
    Close = 0x8,
    Ping = 0x9,
    Pong = 0xA,
};

struct Frame {
    bool fin;
    Op opcode;
    std::string payload; // already unmasked
};

enum class ParseResult {
    NeedMore,
    Bad,
    Got,
};

// Try to parse one frame from the front of `buf`. On Got, populates `out`
// and erases the consumed bytes from `buf`. NeedMore leaves `buf` untouched.
ParseResult ParseFrame(std::string& buf, Frame& out);

// Encode a server-side frame (FIN=1, never masked) ready to be written.
std::string EncodeFrame(Op opcode, std::string_view payload);

} // namespace ws
