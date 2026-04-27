#pragma once

#include <cstdint>
#include <memory>

namespace signaling {

class Player;

using RoomCode = uint64_t;

struct Room {
    RoomCode code = 0;
    std::weak_ptr<Player> host;
    std::weak_ptr<Player> guest;
};

} // namespace signaling
