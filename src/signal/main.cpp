// signal — WebRTC signaling server (matchmaker)
//
// Native C++ binary. Will accept two WebSocket connections, pair them,
// and forward SDP offers/answers + ICE candidates between them
// verbatim. Drops out of the picture once their data channel opens.
//
// TODO: literally everything.

#include <cstdio>

int main() {
    std::puts("signal: not implemented yet");
    return 0;
}
