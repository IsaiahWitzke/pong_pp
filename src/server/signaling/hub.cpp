#include "signaling/hub.h"

#include "ws/frame.h"

#include <charconv>

namespace signaling {

namespace {

namespace verb {
// Client -> server.
inline constexpr std::string_view kList      = "LIST";
inline constexpr std::string_view kCreate    = "CREATE";
inline constexpr std::string_view kJoin      = "JOIN";
// Server -> client.
inline constexpr std::string_view kRooms     = "ROOMS";
inline constexpr std::string_view kCreated   = "CREATED";
inline constexpr std::string_view kReady     = "READY";
inline constexpr std::string_view kPeerLeft  = "PEER_LEFT";
inline constexpr std::string_view kError     = "ERROR";
// Both directions.
inline constexpr std::string_view kRelay     = "RELAY";
} // namespace verb

void SendText(ws::Connection& c, std::string_view msg) {
    c.Write(ws::EncodeFrame(ws::Op::Text, msg));
}

std::string RoomsLine(const Rooms& rooms) {
    std::string msg(verb::kRooms);
    for (auto code : rooms.JoinableCodes()) {
        msg += ' ';
        msg += std::to_string(code);
    }
    return msg;
}

// Parse a Rooms::Code from a text body. Returns 0 on failure (codes
// start at 1, so 0 is a safe sentinel).
Rooms::Code ParseCode(std::string_view s) {
    Rooms::Code v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return 0;
    return v;
}

} // namespace

Hub::Hub() : rooms_([this] { BroadcastRoomList(); }) {}

void Hub::OnConnect(std::shared_ptr<ws::Connection> conn) {
    int fd = conn->GetFD();
    connections_.emplace(fd, std::move(conn));
    lobby_.insert(fd);
}

std::shared_ptr<ws::Connection> Hub::Get(int fd) const {
    auto it = connections_.find(fd);
    return it == connections_.end() ? nullptr : it->second;
}

void Hub::OnReadable(int fd) {
    auto c = Get(fd);
    if (!c) return;
    if (c->Read() == ws::Connection::ReadResult::Err) {
        OnDisconnect(fd);
        // ~Connection eventually fires and calls reactor.Remove(fd).
    }
}

void Hub::OnMessage(int fd, std::string_view msg) {
    // Tolerate line-oriented clients (e.g. `websocat`) that include a
    // trailing newline in each frame.
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.remove_suffix(1);
    }

    auto sp = msg.find(' ');
    auto v = msg.substr(0, sp);
    auto body = (sp == std::string_view::npos) ? std::string_view{} : msg.substr(sp + 1);

    if (v == verb::kList) {
        HandleList(fd);
    } else if (v == verb::kCreate) {
        HandleCreate(fd);
    } else if (v == verb::kJoin) {
        HandleJoin(fd, body);
    } else if (v == verb::kRelay) {
        HandleRelay(fd, body);
    } else {
        auto it = connections_.find(fd);
        if (it != connections_.end())
            SendText(*it->second, std::string(verb::kError) + " unknown_verb");
    }
}

void Hub::OnDisconnect(int fd) {
    auto room_code = rooms_.RoomFor(fd);
    if (room_code) {
        // Notify the surviving peer if any, and put them back in the lobby.
        auto peer = rooms_.Peer(fd);
        if (peer) {
            SendText(*peer, verb::kPeerLeft);
            lobby_.insert(peer->GetFD());
        }
        rooms_.DeleteRoom(*room_code);
    }
    lobby_.erase(fd);
    connections_.erase(fd);
}

void Hub::BroadcastRoomList() {
    auto msg = RoomsLine(rooms_);
    for (int fd : lobby_) {
        auto it = connections_.find(fd);
        if (it != connections_.end()) SendText(*it->second, msg);
    }
}

void Hub::HandleList(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    SendText(*it->second, RoomsLine(rooms_));
}

void Hub::HandleCreate(int fd) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    auto conn = it->second;

    auto code = rooms_.NewRoom(conn);
    lobby_.erase(fd);
    SendText(*conn, std::string(verb::kCreated) + ' ' + std::to_string(code));
}

void Hub::HandleJoin(int fd, std::string_view code) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    auto guest = it->second;

    auto parsed = ParseCode(code);
    if (parsed == 0 || !rooms_.Join(parsed, guest)) {
        SendText(*guest, std::string(verb::kError) + " join_failed");
        return;
    }

    lobby_.erase(fd);

    // Notify both peers.
    auto host = rooms_.Peer(fd);
    if (host) SendText(*host, std::string(verb::kReady) + " host");
    SendText(*guest, std::string(verb::kReady) + " guest");
}

void Hub::HandleRelay(int fd, std::string_view body) {
    auto peer = rooms_.Peer(fd);
    if (!peer) {
        auto it = connections_.find(fd);
        if (it != connections_.end())
            SendText(*it->second, std::string(verb::kError) + " not_in_room");
        return;
    }
    std::string msg(verb::kRelay);
    msg += ' ';
    msg.append(body);
    SendText(*peer, msg);
}

} // namespace signaling
