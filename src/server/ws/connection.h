#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace ws {

class Connection {
public:
    enum class State {
        Handshake,
        Open,
    };

    enum class ReadResult {
        Ok,
        Err,
    };

    // Called once per complete Text/Binary message received in Open state.
    // The connection passes itself so the handler can identify the sender
    // without keeping a separate fd-keyed lookup table on the side.
    using OnMessageFn =
        std::function<void(Connection& self, std::string_view payload)>;

    // Called exactly once from the destructor, before the fd is closed.
    // Typically used to deregister this connection from an event loop.
    using OnDestructFn = std::function<void()>;

    Connection(int fd, OnDestructFn on_destruct, OnMessageFn on_message);
    ~Connection();

    // Resource-owning: prohibit copy. Copies would close the same fd twice.
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    State GetState() const { return state_; }
    int GetFD() const { return fd_; }

    // Read pending bytes off the socket. While in Handshake state, scan for a
    // complete request and respond. While in Open state, drain complete frames
    // and dispatch them. Returns Err on disconnect, hard error, malformed
    // handshake, malformed frame, or peer-initiated close.
    ReadResult Read();

    // Send the bytes synchronously (loops over partial writes).
    void Write(std::string_view s);

private:
    enum class HandshakePhase {
        NeedMore,
        Bad,
        ResponseSent,
    };

    enum class FramesPhase {
        NeedMore, // not enough bytes for the next frame; come back later
        Bad,      // malformed frame; tear down
        Closed,   // peer sent Close (we acked); tear down
    };

    HandshakePhase HandleHandshake();
    FramesPhase HandleFrames();

    int fd_;
    State state_ = State::Handshake;
    std::string in_buf_;
    OnDestructFn on_destruct_;
    OnMessageFn on_message_;
};

} // namespace ws
