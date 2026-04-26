#include "ws/handshake.h"

#include "util/base64.h"
#include "util/sha1.h"

#include <string_view>

namespace ws {

namespace {

// Magic GUID per RFC 6455 §1.3, concatenated with the client's key before
// hashing to produce Sec-WebSocket-Accept.
constexpr std::string_view kWebSocketGuid =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// Look up an HTTP header value by name in `buf`. Returns "" if absent.
std::string FindHeader(const std::string& buf, std::string_view name) {
    // Headers are framed by CRLF and named like "<name>: <value>".
    std::string needle = "\r\n";
    needle.append(name);
    needle += ": ";

    auto pos = buf.find(needle);
    if (pos == std::string::npos)
        return {};
    pos += needle.size();

    auto end = buf.find("\r\n", pos);
    if (end == std::string::npos)
        return {};
    return buf.substr(pos, end - pos);
}

} // namespace

HandshakeResult ProcessHandshake(std::string& buf, std::string& out_response) {
    auto end = buf.find("\r\n\r\n");
    if (end == std::string::npos)
        return HandshakeResult::NeedMore;

    std::string key = FindHeader(buf, "Sec-WebSocket-Key");
    if (key.empty())
        return HandshakeResult::Bad;

    // Sec-WebSocket-Accept = base64(util::sha1(key + magic GUID))
    std::string concat = key;
    concat.append(kWebSocketGuid);
    std::string accept = util::base64_encode(util::sha1(concat));

    out_response = "HTTP/1.1 101 Switching Protocols\r\n"
                   "Upgrade: websocket\r\n"
                   "Connection: Upgrade\r\n"
                   "Sec-WebSocket-Accept: " +
                   accept +
                   "\r\n"
                   "\r\n";

    // Consume just the request bytes; any frames pipelined behind it
    // (spec-violating but possible) survive for the Open state.
    buf.erase(0, end + 4);
    return HandshakeResult::Done;
}

} // namespace ws
