#include "signaling/rooms.h"

#include <utility>

namespace signaling {

Rooms::Rooms(ChangedFn on_changed) : on_changed_(std::move(on_changed)) {}

Rooms::Code Rooms::NewRoom(std::shared_ptr<ws::Connection> host) {
    // Codes are monotonically increasing integers. Never recycled, so
    // the uniqueness invariant is structural: no collision possible.
    Code code = next_code_++;
    rooms_.emplace(code, Room{.host = std::move(host), .guest = nullptr});
    Notify();
    return code;
}

bool Rooms::DeleteRoom(Code code) {
    if (rooms_.erase(code) == 0) return false;
    Notify();
    return true;
}

bool Rooms::Join(Code code, std::shared_ptr<ws::Connection> guest) {
    auto it = rooms_.find(code);
    if (it == rooms_.end() || it->second.guest != nullptr) return false;
    it->second.guest = std::move(guest);
    // The joinable set just shrank (this room is no longer joinable).
    Notify();
    return true;
}

std::vector<Rooms::Code> Rooms::JoinableCodes() const {
    std::vector<Code> out;
    out.reserve(rooms_.size());
    for (const auto& [code, room] : rooms_) {
        if (!room.guest) out.push_back(code);
    }
    return out;
}

std::shared_ptr<ws::Connection> Rooms::Peer(int fd) const {
    for (const auto& [code, room] : rooms_) {
        if (room.host && room.host->GetFD() == fd) return room.guest;
        if (room.guest && room.guest->GetFD() == fd) return room.host;
    }
    return nullptr;
}

std::optional<Rooms::Code> Rooms::RoomFor(int fd) const {
    for (const auto& [code, room] : rooms_) {
        if ((room.host && room.host->GetFD() == fd) ||
            (room.guest && room.guest->GetFD() == fd)) {
            return code;
        }
    }
    return std::nullopt;
}

} // namespace signaling
