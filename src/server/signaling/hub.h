#pragma once

#include "signaling/rooms.h"
#include "ws/connection.h"

#include <memory>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace signaling {

// Top-level application state. Owns the lobby, the rooms, and the
// per-fd connection registry. Dispatches incoming text frames to the
// appropriate verb handler.
class Hub {
public:
    Hub();

    // A WebSocket has just opened. Hub takes ownership; lifetime is now
    // tied to its presence in connections_ (plus any rooms_ membership).
    void OnConnect(std::shared_ptr<ws::Connection> conn);

    // The reactor reports that this fd has bytes ready. Reads from the
    // connection and runs the protocol; tears down on error.
    void OnReadable(int fd);

    // Look up a connection by fd. Returns a copy of the shared_ptr (so
    // the caller can safely use it for the duration of one operation),
    // or nullptr if no such fd is registered.
    std::shared_ptr<ws::Connection> Get(int fd) const;

    // A complete text frame arrived from the connection with this fd.
    void OnMessage(int fd, std::string_view msg);

    // The connection is closing (TCP FIN, hard error, etc.).
    void OnDisconnect(int fd);

private:
    Rooms rooms_;
    // Master fd -> connection registry; canonical "this fd is alive".
    std::unordered_map<int, std::shared_ptr<ws::Connection>> connections_;
    // Subset of connections_ keys that are not currently in any room.
    std::unordered_set<int> lobby_;

    // Push the current ROOMS list to every connection in the lobby.
    void BroadcastRoomList();

    // Verb handlers.
    void HandleList(int fd);
    void HandleCreate(int fd);
    void HandleJoin(int fd, std::string_view code);
    void HandleRelay(int fd, std::string_view body);
};

} // namespace signaling
