#pragma once

#include <string>

namespace ws {

enum class HandshakeResult {
    NeedMore,    // request bytes incomplete; come back with more data
    Bad,         // request is truly malformed (not a parseable HTTP request)
    NotUpgrade,  // complete HTTP request, but no WS upgrade. `out_response`
                 // contains a 426; caller should write it then close.
    Done,        // 101 response written to `out_response`; request bytes erased.
};

// If `buf` contains a complete WebSocket upgrade request, build the 101
// response in `out_response` and erase the request bytes from `buf`. The
// caller is responsible for actually writing `out_response` to the socket.
// If the request is a complete but non-upgrade HTTP request, fills
// `out_response` with a 426 Upgrade Required and returns NotUpgrade.
HandshakeResult ProcessHandshake(std::string& buf, std::string& out_response);

} // namespace ws
