#include "signaling/hub.h"

#include "ws/frame.h"

#include <cstdio>
#include <random>

namespace signaling {

namespace {

// Generate a random 4-letter uppercase room code.
std::string MakeRoomCode() {
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 25);
    std::string code(4, '\0');
    for (auto& c : code) c = char('A' + dist(rng));
    return code;
}

void SendText(ws::Connection& c, std::string_view msg) {
    c.Write(ws::EncodeFrame(ws::Op::Text, msg));
}

std::string RoomsLine(const Rooms& rooms) {
    std::string msg = "ROOMS";
    for (const auto& code : rooms.JoinableCodes()) {
        msg += ' ';
        msg += code;
    }
    return msg;
}

} // namespace

Hub::Hub() : rooms_([this] { BroadcastRoomList(); }) {}

void Hub::OnConnect(std::shared_ptr<ws::Connection> conn) {
    int fd = conn->GetFD();
    connections_.emplace(fd, std::move(conn));
    lobby_.insert(fd);
    // Don't push initial state proactively; client sends LIST when it
    // wants to know.
}

std::shared_ptr<ws::Connection> Hub::Get(int fd) const {
    auto it = connections_.find(fd);
    return it == connections_.end() ? nullptr : it->second;
}

void Hub::OnMessage(int fd, std::string_view msg) {
    // Tolerate line-oriented clients (e.g. `websocat`) that include a
    // trailing newline in each frame.
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r')) {
        msg.remove_suffix(1);
    }

    auto sp = msg.find(' ');
    auto verb = msg.substr(0, sp);
    auto body = (sp == std::string_view::npos) ? std::string_view{} : msg.substr(sp + 1);

    if (verb == "LIST") {
        HandleList(fd);
    } else if (verb == "CREATE") {
        HandleCreate(fd);
    } else if (verb == "JOIN") {
        HandleJoin(fd, body);
    } else if (verb == "RELAY") {
        HandleRelay(fd, body);
    } else {
        auto it = connections_.find(fd);
        if (it != connections_.end()) SendText(*it->second, "ERROR unknown_verb");
    }
}

void Hub::OnDisconnect(int fd) {
    auto room_code = rooms_.RoomFor(fd);
    if (room_code) {
        // Notify the surviving peer if any, and put them back in the lobby.
        auto peer = rooms_.Peer(fd);
        if (peer) {
            SendText(*peer, "PEER_LEFT");
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

    // Try a few times in case of collision (vanishingly unlikely with 4
    // uppercase letters, but cheap to be defensive).
    std::string code;
    for (int attempt = 0; attempt < 8; ++attempt) {
        code = MakeRoomCode();
        if (rooms_.NewRoom(code, conn)) break;
        code.clear();
    }
    if (code.empty()) {
        SendText(*conn, "ERROR could_not_allocate_room");
        return;
    }

    lobby_.erase(fd);
    SendText(*conn, "CREATED " + code);
}

void Hub::HandleJoin(int fd, std::string_view code) {
    auto it = connections_.find(fd);
    if (it == connections_.end()) return;
    auto guest = it->second;

    std::string code_str(code);
    if (!rooms_.Join(code_str, guest)) {
        SendText(*guest, "ERROR join_failed");
        return;
    }

    lobby_.erase(fd);

    // Notify both peers.
    auto host = rooms_.Peer(fd);
    if (host) SendText(*host, "READY host");
    SendText(*guest, "READY guest");
}

void Hub::HandleRelay(int fd, std::string_view body) {
    auto peer = rooms_.Peer(fd);
    if (!peer) {
        auto it = connections_.find(fd);
        if (it != connections_.end()) SendText(*it->second, "ERROR not_in_room");
        return;
    }
    std::string msg = "RELAY ";
    msg.append(body);
    SendText(*peer, msg);
}

} // namespace signaling
