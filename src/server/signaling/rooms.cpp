#include "signaling/rooms.h"

#include <utility>

namespace signaling {

Rooms::Rooms(ChangedFn on_changed) : on_changed_(std::move(on_changed)) {}

bool Rooms::NewRoom(std::string code, std::shared_ptr<ws::Connection> host) {
    auto [it, ok] = rooms_.emplace(
        std::move(code), Room{.host = std::move(host), .guest = nullptr});
    if (!ok) return false;
    Notify();
    return true;
}

bool Rooms::DeleteRoom(const std::string& code) {
    if (rooms_.erase(code) == 0) return false;
    Notify();
    return true;
}

bool Rooms::Join(const std::string& code, std::shared_ptr<ws::Connection> guest) {
    auto it = rooms_.find(code);
    if (it == rooms_.end() || it->second.guest != nullptr) return false;
    it->second.guest = std::move(guest);
    // The joinable set just shrank (this room is no longer joinable).
    Notify();
    return true;
}

std::vector<std::string> Rooms::JoinableCodes() const {
    std::vector<std::string> out;
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

std::optional<std::string> Rooms::RoomFor(int fd) const {
    for (const auto& [code, room] : rooms_) {
        if ((room.host && room.host->GetFD() == fd) ||
            (room.guest && room.guest->GetFD() == fd)) {
            return code;
        }
    }
    return std::nullopt;
}

} // namespace signaling
