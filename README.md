# pong_pp

Multiplayer Pong, written from scratch — C++ → WebAssembly with no Emscripten,
no SDL, no third-party libraries. The WASM module imports a tiny canvas
drawing API from JavaScript, and exports `init()` / `tick()` which the
browser drives with `requestAnimationFrame`.

## Prerequisites

You need `clang` with the `wasm32` target and `wasm-ld`. Apple's bundled
clang does **not** include the wasm32 target, so install LLVM separately.

**macOS**
```sh
brew install llvm lld
echo 'export PATH="/opt/homebrew/opt/llvm/bin:/opt/homebrew/opt/lld/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc
```
Note: `lld` (which provides `wasm-ld`) is a separate Homebrew formula on macOS and is required for linking.

**WSL2 / Linux**
```sh
sudo apt install clang lld python3
```

Verify:
```sh
clang --version
clang --target=wasm32 --print-targets | grep wasm32
```

## Build

```sh
make
```

Produces `web/pong.wasm`.

## Run

```sh
make serve
```

Then open <http://localhost:8080/>. You should see a white square bouncing
around a black canvas, and `wasm: 42` printed in the browser DevTools
console (proves the JS → WASM import bridge works).

## Layout

```
pong_pp/
├── src/main.cpp     # game logic (freestanding C++, no stdlib)
├── web/index.html   # canvas + module script tag
├── web/loader.js    # JS shim: provides imports, drives tick()
├── web/pong.wasm    # build output (gitignored)
└── Makefile
```

## How the JS↔WASM bridge works

The wasm module declares functions it expects JS to provide:

```cpp
__attribute__((import_name("fill_rect")))
void fill_rect(float x, float y, float w, float h, int r, int g, int b);
```

`loader.js` provides matching implementations under `imports.env`. At
instantiation time, the WebAssembly runtime resolves each import to the
corresponding JS function.

In the other direction, the wasm module exports functions JS can call:

```cpp
__attribute__((export_name("tick")))
void tick() { /* ... */ }
```

These appear on `instance.exports` after `WebAssembly.instantiateStreaming`.

## Next steps toward Pong

- Add paddles + keyboard input (`keydown`/`keyup` in `loader.js`,
  exported `set_paddle(player, y)` in C++)
- Add a WebSocket import so the wasm module can send/receive game-state
  bytes from a server
- Build a separate native server binary that runs the authoritative game
  loop and broadcasts state to both connected clients
