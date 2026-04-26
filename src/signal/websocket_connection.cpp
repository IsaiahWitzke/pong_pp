#include "websocket_connection.h"

#include "base64.h"
#include "sha1.h"

#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace {
// Magic GUID per RFC 6455 §1.3, concatenated with the client's key before
// hashing to produce Sec-WebSocket-Accept.
constexpr std::string_view kWebSocketGuid =
    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
}  // namespace

WebsocketConnection::WebsocketConnection(int fd) : fd(fd) {}

WebsocketConnection::~WebsocketConnection() {
  if (fd >= 0) close(fd);
}

WebsocketConnection::ReadResult WebsocketConnection::Read() {
  char buf[4096];
  ssize_t n = ::read(fd, buf, sizeof buf);
  if (n <= 0) {
    std::printf("signal: client %d disconnected\n", fd);
    return ReadResult::Err;
  }

  in_buf.append(buf, n);

  if (state == State::Handshake &&
      in_buf.find("\r\n\r\n") != std::string::npos) {
    if (!HandleHandshake()) {
      return ReadResult::Err;
    }
  }

  return ReadResult::Ok;
}

void WebsocketConnection::Write(std::string_view s) {
  const char* p = s.data();
  size_t left = s.size();
  while (left > 0) {
    ssize_t n = ::write(fd, p, left);
    if (n < 0) {
      if (errno == EINTR) continue;
      return;  // peer gone or hard error; next Read() will surface it
    }
    p += n;
    left -= n;
  }
}

std::string WebsocketConnection::FindHeader(std::string_view name) const {
  // Headers are framed by CRLF and named like "<name>: <value>".
  std::string needle = "\r\n";
  needle.append(name);
  needle += ": ";

  auto pos = in_buf.find(needle);
  if (pos == std::string::npos) return {};
  pos += needle.size();

  auto end = in_buf.find("\r\n", pos);
  if (end == std::string::npos) return {};
  return in_buf.substr(pos, end - pos);
}

bool WebsocketConnection::HandleHandshake() {
  std::string key = FindHeader("Sec-WebSocket-Key");
  if (key.empty()) {
    std::printf("signal: client %d missing Sec-WebSocket-Key\n", fd);
    return false;
  }

  // Sec-WebSocket-Accept = base64(sha1(key + magic GUID))
  std::string concat = key;
  concat.append(kWebSocketGuid);
  std::string accept = base64_encode(sha1(concat));

  std::string response =
      "HTTP/1.1 101 Switching Protocols\r\n"
      "Upgrade: websocket\r\n"
      "Connection: Upgrade\r\n"
      "Sec-WebSocket-Accept: " + accept + "\r\n"
      "\r\n";
  Write(response);

  std::printf("signal: client %d websocket handshake complete\n", fd);
  state = State::Open;
  in_buf.clear();  // discard HTTP request; future bytes are WS frames
  return true;
}
