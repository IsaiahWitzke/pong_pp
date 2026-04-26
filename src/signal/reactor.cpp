#include "reactor.h"

#include <poll.h>

#include <cerrno>
#include <cstdio>
#include <utility>
#include <vector>

void Reactor::OnReadable(int fd, Handler cb) {
  handlers_[fd] = std::move(cb);
}

void Reactor::Remove(int fd) { handlers_.erase(fd); }

void Reactor::Stop() { running_ = false; }

void Reactor::Run() {
  running_ = true;
  std::vector<pollfd> pfds;
  while (running_) {
    // Rebuild the poll array from currently-registered fds.
    pfds.clear();
    pfds.reserve(handlers_.size());
    for (const auto &[fd, _] : handlers_) {
      pfds.push_back({fd, POLLIN, 0});
    }
    if (pfds.empty())
      break;

    if (poll(pfds.data(), pfds.size(), -1) < 0) {
      if (errno == EINTR)
        continue;
      perror("poll");
      break;
    }

    // Dispatch. Copy the handler before invoking, in case the handler
    // removes itself from the reactor (which would otherwise destroy
    // the std::function we're in the middle of executing).
    for (const auto &pfd : pfds) {
      if (pfd.revents == 0)
        continue;
      auto it = handlers_.find(pfd.fd);
      if (it == handlers_.end())
        continue;
      Handler cb = it->second;
      cb(pfd.revents);
    }
  }
}
