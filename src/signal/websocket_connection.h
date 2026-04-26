#pragma once

#include <string>
#include <string_view>

class WebsocketConnection {
public:
  enum class State {
    Handshake,
    Open,
  };

  enum class ReadResult {
    Ok,
    Err,
  };

  explicit WebsocketConnection(int fd);
  ~WebsocketConnection();

  // Resource-owning: prohibit copy. Copies would close the same fd twice.
  WebsocketConnection(const WebsocketConnection &) = delete;
  WebsocketConnection &operator=(const WebsocketConnection &) = delete;

  State GetState() const { return state; }
  int GetFD() const { return fd; }

  // Read pending bytes off the socket. While in Handshake state, scan for
  // a complete request and respond to it. Returns Err on disconnect, hard
  // error, or a malformed handshake.
  ReadResult Read();

  // Send the bytes synchronously (loops over partial writes).
  void Write(std::string_view s);

private:
  // Look up an HTTP header value in in_buf by name. Returns "" if absent.
  std::string FindHeader(std::string_view name) const;

  // Process a complete handshake request that's currently sitting in
  // in_buf: compute Sec-WebSocket-Accept, send 101 response, transition
  // to Open, clear in_buf. Returns false if the request is malformed.
  bool HandleHandshake();

  int fd;
  State state = State::Handshake;
  std::string in_buf;
};
