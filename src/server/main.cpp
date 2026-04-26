#include "reactor.h"
#include "signaling/hub.h"
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

    std::printf("server: listening on :%u\n", port);
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
    signaling::Hub hub;

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
        std::printf("server: client %d connected from %s:%u\n", cfd, ip,
                    ntohs(peer.sin_port));

        // Per-client handler. ws::Connection handles the WS protocol;
        // application messages go up to the hub. The connection cleans
        // up its own reactor registration via the on_destruct callback,
        // so the lambda below only deals with hub-level lifecycle.
        auto conn = std::make_shared<ws::Connection>(
            cfd,
            [&reactor, fd = cfd] { reactor.Remove(fd); },
            [&hub, fd = cfd](std::string_view msg) { hub.OnMessage(fd, msg); });
        hub.OnConnect(std::move(conn));

        reactor.OnReadable(
            cfd, [&hub, fd = cfd](int /* revents */) { hub.OnReadable(fd); });
    });

    reactor.Run();
    close(lfd);
    return 0;
}
