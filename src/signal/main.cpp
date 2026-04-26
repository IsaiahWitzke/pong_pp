#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int make_listener(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); std::exit(1); }

    // "if this address is in TIME_WAIT, let me bind to it anyway"
    // i.e. to sidestep "bind: Address already in use" errors
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (sockaddr*)&addr, sizeof addr) < 0) { perror("bind"); std::exit(1); }
    if (listen(fd, 16) < 0) { perror("listen"); std::exit(1); }

    std::printf("signal: listening on :%u\n", port);
    return fd;
}

int main() {
    const uint16_t port = std::getenv("PORT") ? std::atoi(std::getenv("PORT")) : 9000;
    int lfd = make_listener(port);

    std::vector<pollfd> fds;
    fds.push_back({lfd, POLLIN, 0});

    char buf[4096];
    for (;;) {
        // block until any of the fds has something interesting to accept/read
        if (poll(fds.data(), fds.size(), -1) < 0) { perror("poll"); break; }

        // Accept new connections on the listener.
        if (fds[0].revents & POLLIN) {
            sockaddr_in peer{};
            socklen_t plen = sizeof peer;
            int cfd = accept(lfd, (sockaddr*)&peer, &plen);
            if (cfd >= 0) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
                std::printf("signal: client %d connected from %s:%u\n",
                            cfd, ip, ntohs(peer.sin_port));
                fds.push_back({cfd, POLLIN, 0});
            }
        }

        // Walk client fds. Iterate from the end so we can erase safely.
        for (size_t i = fds.size(); i-- > 1; ) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;

            ssize_t n = read(fds[i].fd, buf, sizeof buf);
            if (n <= 0) {
                std::printf("signal: client %d disconnected\n", fds[i].fd);
                close(fds[i].fd);
                fds.erase(fds.begin() + i);
                continue;
            }
            // Echo back. (Real signaling will route between paired peers instead.)
            write(fds[i].fd, buf, n);
        }
    }
    return 0;
}
