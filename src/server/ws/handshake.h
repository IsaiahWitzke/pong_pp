#pragma once

#include <string>

namespace ws {

enum class HandshakeResult {
    NeedMore, // request bytes incomplete; come back with more data
    Bad,      // request is malformed (e.g. missing Sec-WebSocket-Key)
    Done,     // 101 response written to `out_response`; request bytes erased
};

// If `buf` contains a complete WebSocket upgrade request, build the 101
// response in `out_response` and erase the request bytes from `buf`. The
// caller is responsible for actually writing `out_response` to the socket.
HandshakeResult ProcessHandshake(std::string& buf, std::string& out_response);

} // namespace ws
