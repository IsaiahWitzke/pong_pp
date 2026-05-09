// Top-level browser entrypoint. Instantiates pong.wasm, owns the
// Menu/Waiting/Game screen state, bridges:
//   - Signaling events (lobby + room mgmt) → DOM updates
//   - Peer DataChannel ↔ WASM (start_game / on_peer_message / stop_game)
//   - Canvas drawing imports called from WASM tick()
// All gameplay logic lives in WASM; this file is glue.

import { Rtc } from "./rtc.js";
import { Signaling } from "./signaling.js";
import { Screen, type WasmExports } from "./types.js";

// ── Constants & DOM handles ───────────────────────────────────────────────

const SIGNAL_URL =
    location.hostname === "localhost" || location.hostname === "127.0.0.1"
        ? "ws://localhost:9000"
        : "wss://pong-signal-3tez3w6v5q-ue.a.run.app";

const canvas = document.getElementById("canvas") as HTMLCanvasElement;
const ctx = canvas.getContext("2d")!;

// ── Screen state ─────────────────────────────────────────────────────────

let currentScreen: Screen = Screen.Menu;

function showScreen(next: Screen): void {
    currentScreen = next;
    const ids = ["screen-menu", "screen-waiting", "screen-game"] as const;
    ids.forEach((id, i) => {
        document.getElementById(id)!.classList.toggle("active", i === next);
    });
}

function setRtcStatus(text: string, cls: "open" | "closed" | null): void {
    const el = document.getElementById("rtc-status");
    if (!el) return;
    el.textContent = text;
    el.classList.remove("open", "closed");
    if (cls) el.classList.add(cls);
}

function showError(msg: string): void {
    const b = document.getElementById("error-banner")!;
    b.textContent = msg;
    b.classList.add("visible");
    setTimeout(() => b.classList.remove("visible"), 4000);
}

function setButtonsDisabled(disabled: boolean): void {
    (document.getElementById("btn-host") as HTMLButtonElement).disabled = disabled;
    (document.getElementById("btn-join") as HTMLButtonElement).disabled = disabled;
    document.querySelectorAll<HTMLButtonElement>(".join-room-btn").forEach((b) => {
        b.disabled = disabled;
    });
}

function renderRoomList(codes: readonly string[]): void {
    const el = document.getElementById("room-list")!;
    el.innerHTML = "";
    if (codes.length === 0) {
        const span = document.createElement("span");
        span.className = "empty";
        span.textContent = "no open rooms";
        el.appendChild(span);
        return;
    }
    for (const code of codes) {
        const btn = document.createElement("button");
        btn.className = "btn join-room-btn";
        btn.textContent = `JOIN ROOM ${code}`;
        btn.addEventListener("click", () => signaling.join(code));
        el.appendChild(btn);
    }
}

// ── WASM ↔ JS plumbing ───────────────────────────────────────────────────

let wasm: WasmExports;

// Read len bytes from WASM linear memory at ptr, decode as UTF-8.
function readMem(ptr: number, len: number): string {
    return new TextDecoder().decode(new Uint8Array(wasm.memory.buffer, ptr, len));
}

// Encode str as UTF-8 and write into msg_buf. Returns bytes written.
function writeMsg(str: string): number {
    const enc = new TextEncoder().encode(str);
    const ptr = wasm.get_msg_buf();
    const cap = wasm.get_msg_buf_size();
    const len = Math.min(enc.length, cap - 1);
    new Uint8Array(wasm.memory.buffer, ptr, len).set(enc.subarray(0, len));
    return len;
}

// Imports satisfied by JS, called from WASM. Note: `wasm` is captured by
// closure but not yet bound at module-evaluation time — these closures only
// run after instantiate, by which point it's set.
const imports = {
    env: {
        // libc-ish builtins clang emits even with -nostdlib (struct copies,
        // array init, etc. lower to memcpy/memset/memmove).
        memcpy: (dst: number, src: number, n: number) => {
            new Uint8Array(wasm.memory.buffer).copyWithin(dst, src, src + n);
            return dst;
        },
        memmove: (dst: number, src: number, n: number) => {
            new Uint8Array(wasm.memory.buffer).copyWithin(dst, src, src + n);
            return dst;
        },
        memset: (dst: number, val: number, n: number) => {
            new Uint8Array(wasm.memory.buffer, dst, n).fill(val);
            return dst;
        },
        memcmp: (a: number, b: number, n: number) => {
            const heap = new Uint8Array(wasm.memory.buffer);
            for (let i = 0; i < n; i++) {
                const d = heap[a + i]! - heap[b + i]!;
                if (d !== 0) return d;
            }
            return 0;
        },

        // Canvas
        clear_canvas: () => {
            ctx.fillStyle = "#000";
            ctx.fillRect(0, 0, canvas.width, canvas.height);
        },
        fill_rect: (x: number, y: number, w: number, h: number, r: number, g: number, b: number) => {
            ctx.fillStyle = `rgb(${r},${g},${b})`;
            ctx.fillRect(x, y, w, h);
        },
        fill_text: (x: number, y: number, ptr: number, len: number, size: number, r: number, g: number, b: number) => {
            ctx.fillStyle = `rgb(${r},${g},${b})`;
            ctx.font = `${size}px ui-monospace, monospace`;
            ctx.fillText(readMem(ptr, len), x, y);
        },
        console_log_int: (v: number) => console.log("wasm:", v),

        // Current paddle direction. -1 = up, 0 = neutral, +1 = down.
        // Polled by WASM each tick from move_local_paddle().
        paddle_input: () => paddleDir,

        // Send bytes from WASM linear memory to the peer over the data channel.
        peer_send: (ptr: number, len: number) => {
            rtc.send(readMem(ptr, len));
        },
    },
};

// ── Keyboard input ───────────────────────────────────────────────────────
//
// Track up/down keys at module scope so the `paddle_input` import can read
// them synchronously each tick. We accept either WASD or the arrow keys;
// arrow keys are preventDefault-ed so the page doesn't scroll while playing.
let paddleDir: -1 | 0 | 1 = 0;
const pressed = { up: false, down: false };

function isUpKey(e: KeyboardEvent): boolean {
    return e.key === "ArrowUp" || e.key === "w" || e.key === "W";
}
function isDownKey(e: KeyboardEvent): boolean {
    return e.key === "ArrowDown" || e.key === "s" || e.key === "S";
}
function recomputeDir(): void {
    paddleDir = pressed.up && !pressed.down ? -1
              : pressed.down && !pressed.up ? 1
              : 0;
}
window.addEventListener("keydown", (e) => {
    if (isUpKey(e))        { pressed.up   = true; e.preventDefault(); recomputeDir(); }
    else if (isDownKey(e)) { pressed.down = true; e.preventDefault(); recomputeDir(); }
});
window.addEventListener("keyup", (e) => {
    if (isUpKey(e))        { pressed.up   = false; recomputeDir(); }
    else if (isDownKey(e)) { pressed.down = false; recomputeDir(); }
});

// ── Bootstrap ────────────────────────────────────────────────────────────

const result = await WebAssembly.instantiateStreaming(fetch("pong.wasm"), imports);
wasm = result.instance.exports as unknown as WasmExports;
wasm.init();

const signaling = new Signaling(SIGNAL_URL);
const rtc = new Rtc(signaling);

// Signaling lifecycle → UI.
signaling.onOpen = () => {
    setButtonsDisabled(false);
    signaling.list();
};
signaling.onClose = () => {
    rtc.close();
    if (currentScreen === Screen.Game) wasm.stop_game();
    showError("disconnected — reload to reconnect");
};
signaling.onWsError = () => showError("connection error — reload to reconnect");
signaling.onRooms = (codes) => {
    if (currentScreen === Screen.Menu) renderRoomList(codes);
};
signaling.onCreated = (code) => {
    showScreen(Screen.Waiting);
    document.getElementById("waiting-code")!.textContent = code;
};
signaling.onReady = (role) => {
    showScreen(Screen.Game);
    setRtcStatus("connecting…", null);
    rtc.init(role);
    wasm.start_game(role);
};
signaling.onPeerLeft = () => {
    rtc.close();
    wasm.stop_game();
    showScreen(Screen.Menu);
    setButtonsDisabled(false);
    showError("opponent left the game");
    signaling.list();
};
signaling.onError = (reason) => {
    if (reason === "join_failed") showError("room not found or already full");
    else if (reason === "could_not_allocate_room") showError("could not create room");
    else showError("server error");
    setButtonsDisabled(false);
};

// DataChannel ↔ WASM.
rtc.onOpen = () => setRtcStatus("p2p connected", "open");
rtc.onClose = () => setRtcStatus("p2p closed", "closed");
rtc.onMessage = (text) => {
    const len = writeMsg(text);
    wasm.on_peer_message(len);
};
rtc.onConnectionStateChange = (state) => {
    if (state === "failed" || state === "disconnected") {
        setRtcStatus(`p2p ${state}`, "closed");
    }
};

// ── DOM event wiring ─────────────────────────────────────────────────────

document.getElementById("btn-host")!.addEventListener("click", () => {
    setButtonsDisabled(true);
    signaling.create();
});

document.getElementById("btn-cancel")!.addEventListener("click", () => {
    setButtonsDisabled(true);
    rtc.close();
    if (currentScreen === Screen.Game) wasm.stop_game();
    signaling.close();
    showScreen(Screen.Menu);
    // The WS close handler will fire and surface the disconnect banner;
    // no need to do that here.
});

const joinInput = document.getElementById("join-input") as HTMLInputElement;
function attemptJoin(): void {
    const code = joinInput.value;
    if (!code) {
        showError("enter a room code");
        return;
    }
    if (!/^\d+$/.test(code)) {
        showError("room code must be a number");
        return;
    }
    setButtonsDisabled(true);
    signaling.join(code);
}
document.getElementById("btn-join")!.addEventListener("click", attemptJoin);
joinInput.addEventListener("keydown", (e) => {
    if (e.key === "Enter") attemptJoin();
});
joinInput.addEventListener("input", () => {
    joinInput.value = joinInput.value.replace(/[^0-9]/g, "");
});

// ── rAF loop — always ticks; WASM returns early when not in game ─────────
function frame(): void {
    wasm.tick();
    requestAnimationFrame(frame);
}
requestAnimationFrame(frame);

// Kick everything off.
showScreen(Screen.Menu);
signaling.connect();
