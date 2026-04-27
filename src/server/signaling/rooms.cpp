#include "signaling/rooms.h"

#include "signaling/player.h"

#include <utility>

namespace signaling {

Rooms::Rooms(ChangedFn on_changed) : on_changed_(std::move(on_changed)) {}

std::shared_ptr<Room> Rooms::NewRoom(std::shared_ptr<Player> host) {
    auto room = std::make_shared<Room>();
    room->code = next_code_++;
    rooms_.emplace(room->code, room);
    host->EnterRoomAsHost(room);
    Notify();
    return room;
}

std::shared_ptr<Room> Rooms::Join(Code code, std::shared_ptr<Player> guest) {
    auto it = rooms_.find(code);
    if (it == rooms_.end()) return nullptr;
    auto& room = it->second;
    if (!room->guest.expired()) return nullptr;
    guest->EnterRoomAsGuest(room);
    // The joinable set just shrank (this room is no longer joinable).
    Notify();
    return room;
}

void Rooms::Delete(Code code) {
    if (rooms_.erase(code) == 0) return;
    Notify();
}

std::vector<Rooms::Code> Rooms::JoinableCodes() const {
    std::vector<Code> out;
    out.reserve(rooms_.size());
    for (const auto& [code, room] : rooms_) {
        if (room->guest.expired()) out.push_back(code);
    }
    return out;
}

} // namespace signaling
