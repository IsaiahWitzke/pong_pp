# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
make          # build client (WASM), server (native), and web (TS)
make client   # compile src/client/main.cpp → web/pong.wasm
make server   # compile all src/server/**/*.cpp → bin/server
make web      # compile web/src/*.ts → web/*.js (runs `npm install` first time)
make serve    # build client + web, then start Python HTTP server on :8080
make fmt      # run clang-format on all .cpp and .h files
make clean    # remove web/pong.wasm, bin/server, and emitted web/*.js
```

Prerequisites (macOS): `brew install llvm lld` for the C++ toolchain (`clang++`, `wasm-ld`, `clang-format`); Node 20+ for `tsc` (the `web` target installs typescript locally via `npm install`).

There is no test suite. To test manually: `make && make serve`, then open `http://localhost:8080/`.

## Architecture

Three pieces, one repo:

### Client engine (`src/client/`) — WebAssembly
Freestanding C++ compiled to `wasm32` with `-nostdlib`, no exceptions, no RTTI. Pure game engine: ball/paddle simulation, canvas rendering, peer message demux. Knows nothing about WebSockets, WebRTC, the DOM, or rooms.

- JS→WASM imports (`__attribute__((import_name(...)))`): `clear_canvas`, `fill_rect`, `console_log_int`, `peer_send`.
- WASM→JS exports (`__attribute__((export_name(...)))`): `init`, `tick`, `start_game(role)`, `stop_game`, `on_peer_message(len)`, `get_msg_buf`, `get_msg_buf_size`.
- No C++ stdlib is available; avoid including standard headers.

### Web frontend (`web/`) — TypeScript
Sources under `web/src/`, compiled by `tsc` into ES modules emitted at `web/loader.js` etc. (no bundler). Owns everything that isn't gameplay:

- `web/src/types.ts` — `Role` / `Screen` const-object enums; `WasmExports` interface (hand-maintained mirror of the C++ side).
- `web/src/signaling.ts` — `Signaling` class: WebSocket lifecycle plus the line-oriented verb protocol (`LIST`/`CREATE`/`JOIN`/`RELAY` outbound; `ROOMS`/`CREATED`/`READY`/`RELAY`/`PEER_LEFT`/`ERROR` inbound).
- `web/src/rtc.ts` — `Rtc` class: wraps `RTCPeerConnection` + `RTCDataChannel`. SDP/ICE travel through `Signaling.relay` / `Signaling.onRelay`.
- `web/src/loader.ts` — instantiates `pong.wasm`, owns the Menu/Waiting/Game screen state, wires Signaling events to DOM updates, and bridges the data channel to `start_game` / `on_peer_message` / `stop_game`.

`web/pong.wasm` and the `web/*.js` outputs are build artifacts (.gitignored). `web/src/`, `web/index.html`, `web/package.json`, `web/tsconfig.json`, and `web/package-lock.json` are committed.

### Signaling server (`src/server/`) — native C++20
Zero external dependencies. Implements the WebSocket protocol (RFC 6455) from scratch using BSD sockets and `poll(2)`.

Layers (bottom → top):
1. **Reactor** (`reactor.h/.cpp`) — `poll()`-based event loop; maps fds to read-ready callbacks.
2. **WebSocket protocol** (`ws/`) — `handshake` parses/responds to the HTTP upgrade; `frame` parses/encodes WS frames (masking, opcodes, fragmentation); `connection` is a stateful machine (handshaking → open → closing).
3. **Signaling** (`signaling/`) — `Hub` owns all live connections, maintains the lobby and active rooms; `Room` pairs two connections (host + guest) and relays messages opaquely between them.
4. **Utilities** (`util/`) — SHA-1 and Base64 needed for the WS handshake `Sec-WebSocket-Accept` header.

The server never inspects signaling payloads (SDP offers/answers, ICE candidates). Once both peers connect and exchange credentials, WebRTC takes over peer-to-peer; the server becomes idle for that room.

## Code Style

C++ formatting is enforced by `.clang-format` (LLVM style, 4-space indent, left-aligned pointer declarators). Run `make fmt` before committing. The server targets C++20; the client targets a freestanding C++17-compatible subset.

Include paths: the server is compiled with `-I src/server`, so internal headers are included as `"ws/frame.h"`, `"signaling/hub.h"`, etc. — never with `src/server/` prefix.

TypeScript runs under `strict: true` plus `noUncheckedIndexedAccess` and `verbatimModuleSyntax`. Cross-module imports in TS sources use the emitted `.js` extension (e.g. `import { Signaling } from "./signaling.js"`) so the browser's ES-module loader resolves them without a bundler.
