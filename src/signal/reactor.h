#pragma once

#include <functional>
#include <unordered_map>

// i love js so much that i remade the event loop in c++
// note: doesn't own the fds in handlers_... cleanup is for the API users
class Reactor {
  public:
    using Handler = std::function<void(int revents)>;

    void OnReadable(int fd, Handler cb);
    void Remove(int fd);
    void Stop();
    void Run();

  private:
    std::unordered_map<int, Handler> handlers_;
    bool running_ = false;
};
