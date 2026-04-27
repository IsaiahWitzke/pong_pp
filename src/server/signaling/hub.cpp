#include "signaling/hub.h"

namespace signaling {

Hub::Hub() : rooms_([this] { BroadcastRoomList(); }) {}

void Hub::Register(std::shared_ptr<Player> p) {
    players_.insert(std::move(p));
}

void Hub::Unregister(Player& p) {
    players_.erase(p.shared_from_this());
}

void Hub::BroadcastRoomList() {
    auto msg = RoomsLine(rooms_);
    for (auto& p : players_) {
        if (!p->InRoom()) p->Send(msg);
    }
}

} // namespace signaling
