#pragma once

#include "signaling/room.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace signaling {

class Player;

class Rooms {
public:
    using ChangedFn = std::function<void()>;
    using Code = RoomCode;

    explicit Rooms(ChangedFn on_changed);

    std::shared_ptr<Room> NewRoom(std::shared_ptr<Player> host);
    std::shared_ptr<Room> Join(Code code, std::shared_ptr<Player> guest);
    void Delete(Code code);

    // Codes for rooms that don't yet have a guest.
    std::vector<Code> JoinableCodes() const;

private:
    std::unordered_map<Code, std::shared_ptr<Room>> rooms_;
    Code next_code_ = 1;
    ChangedFn on_changed_;

    void Notify() { on_changed_(); }
};

} // namespace signaling
