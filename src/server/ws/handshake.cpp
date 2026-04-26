#include "ws/handshake.h"

#include "util/base64.h"
#include "util/sha1.h"

#include <cctype>
#include <string_view>

namespace ws {

namespace {

// Magic GUID per RFC 6455 §1.3, concatenated with the client's key before
// hashing to produce Sec-WebSocket-Accept.
constexpr std::string_view kWebSocketGuid =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

// 426 Upgrade Required body for non-WebSocket HTTP clients (e.g. uptime
// probes, browsers navigating directly to the URL).
constexpr std::string_view kNotUpgradeBody =
    "This endpoint accepts WebSocket connections only.\n";

// Case-insensitive HTTP header lookup. RFC 9110 §5.1 specifies header field
// names are case-insensitive; in particular, Cloud Run's Envoy front-end
// lowercases all HTTP/1 headers before forwarding to the backend, so a
// case-sensitive search for "Sec-WebSocket-Key" misses the actual header.
std::string FindHeader(const std::string& buf, std::string_view name) {
    // Walk header lines (separated by CRLF). Skip the request-line; start
    // matching after the first CRLF.
    size_t i = buf.find("\r\n");
    if (i == std::string::npos)
        return {};
    i += 2;

    while (i < buf.size()) {
        size_t line_end = buf.find("\r\n", i);
        if (line_end == std::string::npos || line_end == i)
            break; // end of headers (blank line) or malformed

        size_t colon = buf.find(':', i);
        if (colon == std::string::npos || colon > line_end) {
            i = line_end + 2;
            continue;
        }

        // Compare header name case-insensitively.
        size_t name_len = colon - i;
        if (name_len == name.size()) {
            bool match = true;
            for (size_t k = 0; k < name_len; ++k) {
                unsigned char a = buf[i + k];
                unsigned char b = name[k];
                if (std::tolower(a) != std::tolower(b)) {
                    match = false;
                    break;
                }
            }
            if (match) {
                // Skip ':' and any leading optional whitespace per RFC 9110.
                size_t v = colon + 1;
                while (v < line_end && (buf[v] == ' ' || buf[v] == '\t'))
                    ++v;
                return buf.substr(v, line_end - v);
            }
        }

        i = line_end + 2;
    }
    return {};
}

} // namespace

HandshakeResult ProcessHandshake(std::string& buf, std::string& out_response) {
    auto end = buf.find("\r\n\r\n");
    if (end == std::string::npos)
        return HandshakeResult::NeedMore;

    std::string key = FindHeader(buf, "Sec-WebSocket-Key");
    if (key.empty()) {
        // Complete HTTP request, but not a WS upgrade. Reply with a real
        // 426 so Cloud Run / probes / curl get a well-formed response
        // instead of a connection close (which Cloud Run reports as 503).
        out_response = "HTTP/1.1 426 Upgrade Required\r\n"
                       "Upgrade: websocket\r\n"
                       "Connection: Upgrade\r\n"
                       "Content-Type: text/plain; charset=utf-8\r\n"
                       "Content-Length: " +
                       std::to_string(kNotUpgradeBody.size()) +
                       "\r\n"
                       "\r\n";
        out_response.append(kNotUpgradeBody);
        buf.erase(0, end + 4);
        return HandshakeResult::NotUpgrade;
    }

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
