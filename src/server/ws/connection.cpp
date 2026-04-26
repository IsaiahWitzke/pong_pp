#include "ws/connection.h"

#include "ws/frame.h"
#include "ws/handshake.h"

#include <unistd.h>

#include <cerrno>
#include <cstdio>

namespace ws {

Connection::Connection(int fd, OnDestructFn on_destruct, OnMessageFn on_message)
    : fd_(fd), on_destruct_(std::move(on_destruct)),
      on_message_(std::move(on_message)) {}

Connection::~Connection() {
    if (on_destruct_)
        on_destruct_();
    if (fd_ >= 0)
        close(fd_);
}

Connection::ReadResult Connection::Read() {
    char buf[4096];
    ssize_t n = ::read(fd_, buf, sizeof buf);
    if (n <= 0) {
        std::printf("server: client %d disconnected\n", fd_);
        return ReadResult::Err;
    }

    in_buf_.append(buf, n);

    switch (state_) {
    case State::Handshake:
        switch (HandleHandshake()) {
        case HandshakePhase::NeedMore:
            break;
        case HandshakePhase::Bad:
            return ReadResult::Err;
        case HandshakePhase::ResponseSent:
            state_ = State::Open;
            break;
        }
        break;
    case State::Open:
        switch (HandleFrames()) {
        case FramesPhase::NeedMore:
            break;
        case FramesPhase::Bad:
        case FramesPhase::Closed:
            return ReadResult::Err;
        }
        break;
    }

    return ReadResult::Ok;
}

void Connection::Write(std::string_view s) {
    const char* p = s.data();
    size_t left = s.size();
    while (left > 0) {
        ssize_t n = ::write(fd_, p, left);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return; // peer gone or hard error; next Read() will surface it
        }
        p += n;
        left -= n;
    }
}

Connection::HandshakePhase Connection::HandleHandshake() {
    std::string response;
    switch (ProcessHandshake(in_buf_, response)) {
    case HandshakeResult::NeedMore:
        return HandshakePhase::NeedMore;
    case HandshakeResult::Bad:
        std::printf("server: client %d malformed handshake\n", fd_);
        return HandshakePhase::Bad;
    case HandshakeResult::Done:
        Write(response);
        std::printf("server: client %d websocket handshake complete\n", fd_);
        return HandshakePhase::ResponseSent;
    }
    return HandshakePhase::Bad; // unreachable; silences compiler warning
}

Connection::FramesPhase Connection::HandleFrames() {
    while (true) {
        Frame f;
        switch (ParseFrame(in_buf_, f)) {
        case ParseResult::NeedMore:
            return FramesPhase::NeedMore;
        case ParseResult::Bad:
            std::printf("server: client %d malformed frame\n", fd_);
            return FramesPhase::Bad;
        case ParseResult::Got:
            break;
        }

        // Fragmentation isn't supported; browsers don't fragment small
        // messages.
        // TODO: wrap frame parsing / message building with a stateful Assembler class
        if (!f.fin || f.opcode == Op::Continuation) {
            std::printf(
                "server: client %d sent fragmented frame (unsupported)\n", fd_);
            return FramesPhase::Bad;
        }

        switch (f.opcode) {
        case Op::Text:
        case Op::Binary:
            if (on_message_) {
                on_message_(f.payload);
            } else {
                std::printf(
                    "server: client %d frame discarded (no handler): %.*s\n",
                    fd_, int(f.payload.size()), f.payload.data());
            }
            break;
        case Op::Ping:
            // Echo payload back as Pong, per RFC 6455 §5.5.2.
            Write(EncodeFrame(Op::Pong, f.payload));
            break;
        case Op::Pong:
            // Unsolicited or response to our keepalive; ignore.
            break;
        case Op::Close:
            // Acknowledge with our own empty Close, then tear down.
            Write(EncodeFrame(Op::Close, ""));
            std::printf("server: client %d sent close frame\n", fd_);
            return FramesPhase::Closed;
        case Op::Continuation:
            // Already handled by the fragmentation check above.
            return FramesPhase::Bad;
        }
    }
}

} // namespace ws
