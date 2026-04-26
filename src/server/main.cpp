#include "reactor.h"
#include "ws/connection.h"

#include <arpa/inet.h>
#include <memory>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>

static int make_listener(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        std::exit(1);
    }

    // "if this address is in TIME_WAIT, let me bind to it anyway"
    // i.e. to sidestep "bind: Address already in use" errors
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(fd, (sockaddr*)&addr, sizeof addr) < 0) {
        perror("bind");
        std::exit(1);
    }
    if (listen(fd, 16) < 0) {
        perror("listen");
        std::exit(1);
    }

    std::printf("signal: listening on :%u\n", port);
    return fd;
}

int main() {
    // Don't die when a peer disconnects mid-write; we'll see EPIPE on the
    // next read/write and clean up there.
    signal(SIGPIPE, SIG_IGN);

    const uint16_t port =
        std::getenv("PORT") ? std::atoi(std::getenv("PORT")) : 9000;
    int lfd = make_listener(port);

    Reactor reactor;

    // Listener: on POLLIN, accept and register a per-client handler.
    reactor.OnReadable(lfd, [&](int revents) {
        if (!(revents & POLLIN))
            return;

        sockaddr_in peer{};
        socklen_t plen = sizeof peer;
        int cfd = accept(lfd, (sockaddr*)&peer, &plen);
        if (cfd < 0)
            return;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        std::printf("signal: client %d connected from %s:%u\n", cfd, ip,
                    ntohs(peer.sin_port));

        // Per-client handler. The handshake is now handled inside
        // ws::Connection::Read; the lambda just relays Read's verdict.
        auto conn = std::make_shared<ws::Connection>(cfd);
        reactor.OnReadable(cfd, [&reactor, conn](int /* revents */) {
            if (conn->Read() == ws::Connection::ReadResult::Err) {
                reactor.Remove(conn->GetFD());
            }
        });
    });

    reactor.Run();
    close(lfd);
    return 0;
}
