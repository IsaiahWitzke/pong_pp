// Pong client engine. All non-gameplay concerns (lobby protocol, WebRTC
// handshake, DOM, screen state) live in TypeScript on the JS side. This
// module just renders to canvas, simulates ball/paddle state, and
// exchanges gameplay bytes with the peer through the data channel.
//
// Wire contract (see web/src/types.ts WasmExports):
//   JS  ─start_game(role)─▶ WASM   on READY (role: 0 = host, 1 = guest)
//   JS  ─stop_game()─────▶ WASM   on peer_left / cancel / ws_close
//   JS  ─on_peer_message(len)─▶ WASM    after writing peer bytes into msg_buf
//   WASM ─peer_send(ptr, len)─▶ JS     to send bytes over the data channel

extern "C" {
// ── Canvas ────────────────────────────────────────────────────────────────
__attribute__((import_name("clear_canvas"))) void clear_canvas();
__attribute__((import_name("fill_rect"))) void
fill_rect(float x, float y, float w, float h, int r, int g, int b);
__attribute__((import_name("console_log_int"))) void console_log_int(int v);

// ── Peer data channel ─────────────────────────────────────────────────────
__attribute__((import_name("peer_send"))) void peer_send(const char* ptr,
                                                         int len);
}

// ── Constants ─────────────────────────────────────────────────────────────
constexpr float CANVAS_W = 800.0f;
constexpr float CANVAS_H = 600.0f;
constexpr float BALL_SIZE = 16.0f;

// ── Application state ─────────────────────────────────────────────────────
static bool in_game;
static int role; // 0 = host, 1 = guest. Mirrors web/src/types.ts Role.
static float ball_x, ball_y, ball_vx, ball_vy;

// JS writes peer messages into msg_buf before calling on_peer_message.
static char msg_buf[512];

// ── Exports ───────────────────────────────────────────────────────────────
extern "C" {

// JS writes peer-message bytes into msg_buf before calling on_peer_message.
__attribute__((export_name("get_msg_buf"))) int get_msg_buf() {
    return (int)(long)msg_buf;
}

__attribute__((export_name("get_msg_buf_size"))) int get_msg_buf_size() {
    return (int)sizeof(msg_buf);
}

__attribute__((export_name("init"))) void init() {
    in_game = false;
    role = 0;
    ball_x = CANVAS_W / 2.0f;
    ball_y = CANVAS_H / 2.0f;
    ball_vx = 3.5f;
    ball_vy = 2.5f;
}

__attribute__((export_name("start_game"))) void start_game(int r) {
    role = r;
    ball_x = CANVAS_W / 2.0f;
    ball_y = CANVAS_H / 2.0f;
    ball_vx = 3.5f;
    ball_vy = 2.5f;
    in_game = true;
}

__attribute__((export_name("stop_game"))) void stop_game() { in_game = false; }

// JS always calls this on every rAF; we early-return when not in a match.
__attribute__((export_name("tick"))) void tick() {
    if (!in_game)
        return;
    ball_x += ball_vx;
    ball_y += ball_vy;
    if (ball_x < 0.0f || ball_x > CANVAS_W - BALL_SIZE)
        ball_vx = -ball_vx;
    if (ball_y < 0.0f || ball_y > CANVAS_H - BALL_SIZE)
        ball_vy = -ball_vy;
    clear_canvas();
    fill_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE, 255, 255, 255);
}

// Peer wrote `len` bytes into msg_buf. No engine-level protocol yet; this
// is the entry point for the eventual paddle-input / state-sync wire format.
__attribute__((export_name("on_peer_message"))) void
on_peer_message(int /*len*/) {
    // TODO: parse gameplay protocol (paddle input, ball/state corrections,
    // etc.)
}

} // extern "C"
