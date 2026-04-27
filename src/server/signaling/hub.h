#pragma once

#include "signaling/player.h"
#include "signaling/rooms.h"

#include <memory>
#include <unordered_set>

namespace signaling {

class Hub {
public:
    Hub();

    signaling::Rooms& Rooms() { return rooms_; }

    void Register(std::shared_ptr<Player> p);
    void Unregister(Player& p);

private:
    signaling::Rooms rooms_;
    std::unordered_set<std::shared_ptr<Player>> players_;

    // Send the current room-list to every Player not currently in a room.
    void BroadcastRoomList();
};

} // namespace signaling
