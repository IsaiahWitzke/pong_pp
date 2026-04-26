#pragma once

#include "ws/connection.h"

#include <cstdint>
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
    using Code = uint64_t;

    explicit Rooms(ChangedFn on_changed);

    // Allocate a fresh, unique room code and create a room with `host`
    // as its (only) member. Returns the new code (always > 0).
    Code NewRoom(std::shared_ptr<ws::Connection> host);
    bool DeleteRoom(Code code);
    bool Join(Code code, std::shared_ptr<ws::Connection> guest);

    std::vector<Code> JoinableCodes() const;

    // Look up the peer for a given fd. Returns nullptr if no peer.
    std::shared_ptr<ws::Connection> Peer(int fd) const;

    // Find which room (if any) holds this fd.
    std::optional<Code> RoomFor(int fd) const;

private:
    struct Room {
        std::shared_ptr<ws::Connection> host;
        std::shared_ptr<ws::Connection> guest;
    };

    std::unordered_map<Code, Room> rooms_;
    Code next_code_ = 1;
    ChangedFn on_changed_;

    void Notify() { on_changed_(); }
};

} // namespace signaling
