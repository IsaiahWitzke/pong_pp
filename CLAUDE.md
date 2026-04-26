# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make          # build both client (WASM) and server (native)
make client   # compile src/client/main.cpp → web/pong.wasm
make server   # compile all src/server/**/*.cpp → bin/server
make serve    # start Python HTTP server on localhost:8080 (serves web/)
make fmt      # run clang-format on all .cpp and .h files
make clean    # remove web/pong.wasm and bin/server
```

Prerequisites (macOS): `brew install llvm lld` — the Makefile expects `clang++`, `wasm-ld`, and `clang-format` from the LLVM toolchain on `PATH`.

There is no test suite. To test manually: `make && make serve`, then open `http://localhost:8080/`.

## Architecture

Two entirely separate programs share a single repo:

### Client (`src/client/`) — WebAssembly game
Freestanding C++ compiled to `wasm32` with `-nostdlib`, no exceptions, no RTTI. The JS runtime (`web/loader.js`) instantiates the WASM module and drives the game loop via `requestAnimationFrame`.

- JS→WASM imports are declared with `__attribute__((import_name(...)))`: `clear_canvas`, `fill_rect`, `console_log_int`.
- WASM→JS exports are marked with `__attribute__((export_name(...)))`: `init()` and `tick()`.
- No C++ stdlib is available; avoid including standard headers.

### Server (`src/server/`) — WebSocket signaling server
Native C++20, zero external dependencies. Implements the WebSocket protocol (RFC 6455) from scratch using BSD sockets and `poll(2)`.

Layers (bottom → top):
1. **Reactor** (`reactor.h/.cpp`) — `poll()`-based event loop; maps fds to read-ready callbacks.
2. **WebSocket protocol** (`ws/`) — `handshake` parses/responds to the HTTP upgrade; `frame` parses/encodes WS frames (masking, opcodes, fragmentation); `connection` is a stateful machine (handshaking → open → closing).
3. **Signaling** (`signaling/`) — `Hub` owns all live connections, maintains the lobby and active rooms; `Room` pairs two connections (host + guest) and relays messages opaquely between them.
4. **Utilities** (`util/`) — SHA-1 and Base64 needed for the WS handshake `Sec-WebSocket-Accept` header.

The server never inspects signaling payloads (SDP offers/answers, ICE candidates). Once both peers connect and exchange credentials, WebRTC takes over peer-to-peer; the server becomes idle for that room.

### Web frontend (`web/`)
`index.html` + `loader.js` only. `loader.js` provides the WASM import object (canvas bridge) and wires up the `requestAnimationFrame` loop. `web/pong.wasm` is a build output (.gitignored).

## Code Style

Formatting is enforced by `.clang-format` (LLVM style, 4-space indent, left-aligned pointer declarators). Run `make fmt` before committing. The server targets C++20; the client targets a freestanding C++17-compatible subset.

Include paths: the server is compiled with `-I src/server`, so internal headers are included as `"ws/frame.h"`, `"signaling/hub.h"`, etc. — never with `src/server/` prefix.
