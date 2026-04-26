#pragma once

#include "ws/connection.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace signaling {

// Owns the set of active rooms. Every mutation (NewRoom / DeleteRoom /
// Join) calls the on_changed_ callback so observers can react to the
// updated state without callers having to remember.
class Rooms {
public:
    using ChangedFn = std::function<void()>;

    explicit Rooms(ChangedFn on_changed);

    // Returns false if the code is already taken.
    bool NewRoom(std::string code, std::shared_ptr<ws::Connection> host);

    // Returns false if no such room exists.
    bool DeleteRoom(const std::string& code);

    // Returns false if the room is full or doesn't exist.
    bool Join(const std::string& code, std::shared_ptr<ws::Connection> guest);

    // Snapshot of currently-joinable rooms (no guest yet).
    std::vector<std::string> JoinableCodes() const;

    // Look up the peer for a given fd. Returns nullptr if no peer.
    std::shared_ptr<ws::Connection> Peer(int fd) const;

    // Find which room (if any) holds this fd.
    std::optional<std::string> RoomFor(int fd) const;

private:
    struct Room {
        std::shared_ptr<ws::Connection> host;
        std::shared_ptr<ws::Connection> guest;
    };

    std::unordered_map<std::string, Room> rooms_;
    ChangedFn on_changed_;

    void Notify() { on_changed_(); }
};

} // namespace signaling
