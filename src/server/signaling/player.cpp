#include "signaling/player.h"

#include "signaling/hub.h"
#include "signaling/rooms.h"
#include "ws/frame.h"

#include <charconv>
#include <utility>

namespace signaling {

namespace {

namespace verb {
// Client -> server.
inline constexpr std::string_view kList     = "LIST";
inline constexpr std::string_view kCreate   = "CREATE";
inline constexpr std::string_view kJoin     = "JOIN";
// Both directions.
inline constexpr std::string_view kRelay    = "RELAY";
// Server -> client.
inline constexpr std::string_view kRooms    = "ROOMS";
inline constexpr std::string_view kCreated  = "CREATED";
inline constexpr std::string_view kReady    = "READY";
inline constexpr std::string_view kPeerLeft = "PEER_LEFT";
inline constexpr std::string_view kError    = "ERROR";
} // namespace verb

// Parse a room code from a text body. Returns 0 on failure; codes start
// at 1 so 0 is a safe sentinel.
RoomCode ParseCode(std::string_view s) {
    RoomCode v = 0;
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size()) return 0;
    return v;
}

// The other occupant of `room`, or nullptr if `self` is alone (or its
// peer's weak_ptr has expired). Pointer comparison is on Player identity.
std::shared_ptr<Player> OtherPlayer(const Room& room, const Player& self) {
    if (auto h = room.host.lock(); h && h.get() != &self) return h;
    if (auto g = room.guest.lock(); g && g.get() != &self) return g;
    return nullptr;
}

} // namespace

std::string RoomsLine(const Rooms& rooms) {
    std::string msg(verb::kRooms);
    for (auto code : rooms.JoinableCodes()) {
        msg += ' ';
        msg += std::to_string(code);
    }
    return msg;
}

Player::Player(int fd, ws::Connection::OnDestructFn on_destruct, Hub& hub)
    : hub_(&hub),
      conn_(std::make_shared<ws::Connection>(
          fd, std::move(on_destruct),
          // Capturing `this` is safe: this lambda lives inside conn_,
          // and conn_'s lifetime is bounded by Player's lifetime.
          [this](ws::Connection&, std::string_view msg) { OnMessage(msg); })) {}

void Player::Send(std::string_view msg) {
    conn_->Write(ws::EncodeFrame(ws::Op::Text, msg));
}

void Player::EnterRoomAsHost(std::shared_ptr<Room> r) {
    r->host = weak_from_this();
    room_ = std::move(r);
}

void Player::EnterRoomAsGuest(std::shared_ptr<Room> r) {
    r->guest = weak_from_this();
    room_ = std::move(r);
}

void Player::LeaveRoom() { room_.reset(); }

void Player::Cleanup() {
    if (auto room = room_) {
        if (auto peer = OtherPlayer(*room, *this)) {
            peer->LeaveRoom();
            peer->Send(verb::kPeerLeft);
        }
        hub_->Rooms().Delete(room->code);
        room_.reset();
    }
    hub_->Unregister(*this);
}

void Player::OnMessage(std::string_view msg) {
    // Tolerate line-oriented clients (e.g. `websocat`) that include a
    // trailing newline in each frame.
    while (!msg.empty() && (msg.back() == '\n' || msg.back() == '\r'))
        msg.remove_suffix(1);

    auto sp = msg.find(' ');
    auto v = msg.substr(0, sp);
    auto body =
        (sp == std::string_view::npos) ? std::string_view{} : msg.substr(sp + 1);

    if      (v == verb::kList)   Send(RoomsLine(hub_->Rooms()));
    else if (v == verb::kCreate) HandleCreate();
    else if (v == verb::kJoin)   HandleJoin(body);
    else if (v == verb::kRelay)  HandleRelay(body);
    else                         Send(std::string(verb::kError) + " unknown_verb");
}

void Player::HandleCreate() {
    auto room = hub_->Rooms().NewRoom(shared_from_this());
    Send(std::string(verb::kCreated) + ' ' + std::to_string(room->code));
}

void Player::HandleJoin(std::string_view code) {
    auto parsed = ParseCode(code);
    auto room = parsed ? hub_->Rooms().Join(parsed, shared_from_this()) : nullptr;
    if (!room) {
        Send(std::string(verb::kError) + " join_failed");
        return;
    }
    if (auto host = room->host.lock(); host && host.get() != this) {
        host->Send(std::string(verb::kReady) + " host");
    }
    Send(std::string(verb::kReady) + " guest");
}

void Player::HandleRelay(std::string_view body) {
    auto room = room_;
    auto peer = room ? OtherPlayer(*room, *this) : nullptr;
    if (!peer) {
        Send(std::string(verb::kError) + " not_in_room");
        return;
    }
    std::string msg(verb::kRelay);
    msg += ' ';
    msg.append(body);
    peer->Send(msg);
}

} // namespace signaling
