#pragma once

#include "signaling/room.h"
#include "ws/connection.h"

#include <memory>
#include <string>
#include <string_view>

namespace signaling {

class Hub;
class Rooms;

class Player : public std::enable_shared_from_this<Player> {
public:
    Player(int fd, ws::Connection::OnDestructFn on_destruct, Hub& hub);

    ws::Connection& Conn() { return *conn_; }
    const ws::Connection& Conn() const { return *conn_; }

    // Pass-through so callers (the reactor read loop in main.cpp) don't
    // have to reach inside.
    ws::Connection::ReadResult Read() { return conn_->Read(); }

    bool InRoom() const { return room_ != nullptr; }
    std::shared_ptr<Room> CurrentRoom() const { return room_; }

    // Encode `msg` as a Text frame and write it on the underlying socket.
    void Send(std::string_view msg);

    void EnterRoomAsHost(std::shared_ptr<Room> r);
    void EnterRoomAsGuest(std::shared_ptr<Room> r);
    void LeaveRoom();

    void Cleanup();

private:
    // Connection delivered a complete Text/Binary frame. Parse and dispatch.
    void OnMessage(std::string_view msg);

    void HandleCreate();
    void HandleJoin(std::string_view code);
    void HandleRelay(std::string_view body);

    Hub* hub_;
    std::shared_ptr<ws::Connection> conn_;
    std::shared_ptr<Room> room_;
};

// Wire-format "ROOMS code1 code2 ..." listing currently joinable rooms.
// Lives next to the rest of the protocol code; both Player (LIST handler)
// and Hub (BroadcastRoomList) use it.
std::string RoomsLine(const Rooms& rooms);

} // namespace signaling
