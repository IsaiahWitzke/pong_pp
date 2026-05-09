// Pong client engine.
//
// Authority model: host = left paddle (P1) + ball/scoring authority.
// Guest = right paddle (P2). Each tick:
//   - Both peers read local input via paddle_input() and move their own
//     paddle (clamped). The guest's paddle move is therefore locally
//     responsive even before the network catches up.
//   - Guest sends `P <y>` (its paddle's top y-coord, integer pixels).
//   - Host receives `P <y>` to mirror the guest's paddle, simulates the
//     ball + collisions + scoring, and broadcasts `S bx by ly ry ls rs`.
//   - Guest receives `S ...` and overwrites its mirror of every field
//     except its own paddle (already-correct local state).
// All wire numbers are integer pixels / scores in plain ASCII for easy
// inspection in chrome://webrtc-internals.

extern "C" {
// ── Canvas ────────────────────────────────────────────────────────────────
__attribute__((import_name("clear_canvas"))) void clear_canvas();
__attribute__((import_name("fill_rect"))) void
fill_rect(float x, float y, float w, float h, int r, int g, int b);
__attribute__((import_name("fill_text"))) void fill_text(float x, float y,
                                                         const char* ptr,
                                                         int len, int size,
                                                         int r, int g, int b);
__attribute__((import_name("console_log_int"))) void console_log_int(int v);

// ── Input ─────────────────────────────────────────────────────────────────
// Returns -1 (up), 0 (neutral), or +1 (down) based on currently-held keys.
__attribute__((import_name("paddle_input"))) int paddle_input();

// ── Peer data channel ─────────────────────────────────────────────────────
__attribute__((import_name("peer_send"))) void peer_send(const char* ptr,
                                                         int len);
}

// ── Constants ─────────────────────────────────────────────────────────────
constexpr float CANVAS_W = 800.0f;
constexpr float CANVAS_H = 600.0f;
constexpr float BALL_SIZE = 14.0f;
constexpr float PADDLE_W = 12.0f;
constexpr float PADDLE_H = 90.0f;
constexpr float PADDLE_INSET = 24.0f; // distance from canvas edge
constexpr float PADDLE_X_LEFT = PADDLE_INSET;
constexpr float PADDLE_X_RIGHT = CANVAS_W - PADDLE_INSET - PADDLE_W;
constexpr float PADDLE_SPEED = 6.0f;
constexpr float BALL_SPEED_X = 5.0f;
constexpr float BALL_SPEED_Y = 3.0f;

// ── Application state ─────────────────────────────────────────────────────
static bool in_game;
static int
    role; // 0 = host (left), 1 = guest (right). Mirrors web/src/types.ts Role.
static int
    serve_seed; // ticks-since-init; provides cheap variance for ball serves.

static float ball_x, ball_y, ball_vx, ball_vy;
static float left_y, right_y;
static int left_score, right_score;

static char msg_buf[256];  // peer messages (in)
static char send_buf[128]; // peer messages (out)

// ── Stdlib-free numeric helpers ───────────────────────────────────────────
static int int_to_str(int n, char* buf) {
    int pos = 0;
    if (n < 0) {
        buf[pos++] = '-';
        n = -n;
    }
    if (n == 0) {
        buf[pos++] = '0';
        return pos;
    }
    char tmp[16];
    int len = 0;
    while (n > 0) {
        tmp[len++] = '0' + (n % 10);
        n /= 10;
    }
    for (int i = 0; i < len; i++)
        buf[pos++] = tmp[len - 1 - i];
    return pos;
}

// Parse signed int starting at s, stops at non-digit. Writes the parsed
// value to *out and returns the index *just past* the last consumed char.
static int parse_int(const char* s, int len, int start, int* out) {
    int i = start;
    int sign = 1;
    if (i < len && s[i] == '-') {
        sign = -1;
        i++;
    }
    int n = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
        n = n * 10 + (s[i] - '0');
        i++;
    }
    *out = n * sign;
    return i;
}

// Skip a single space (or any non-digit/sign) at index i.
static int skip_sep(const char* s, int len, int i) {
    while (i < len && s[i] != '-' && (s[i] < '0' || s[i] > '9'))
        i++;
    return i;
}

// ── Game logic ────────────────────────────────────────────────────────────
static void reset_ball(int direction) {
    ball_x = CANVAS_W / 2.0f - BALL_SIZE / 2.0f;
    ball_y = CANVAS_H / 2.0f - BALL_SIZE / 2.0f;
    ball_vx = direction * BALL_SPEED_X;
    // Cheap variance: alternate vy sign with each serve.
    ball_vy = (serve_seed & 1) ? BALL_SPEED_Y : -BALL_SPEED_Y;
    serve_seed++;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void move_local_paddle() {
    int dir = paddle_input();
    float* my_y = (role == 0) ? &left_y : &right_y;
    *my_y = clampf(*my_y + dir * PADDLE_SPEED, 0.0f, CANVAS_H - PADDLE_H);
}

// AABB hit test between ball and a paddle, given the paddle's left x.
// Reflects ball_vx and biases ball_vy toward the offset from paddle center
// so players can angle returns. Only flips ball_vx if it's heading INTO
// the paddle, so we don't double-bounce on consecutive frames.
static void try_paddle_collide(float paddle_x, float paddle_y,
                               bool ball_going_into_paddle) {
    if (ball_x + BALL_SIZE < paddle_x)
        return;
    if (ball_x > paddle_x + PADDLE_W)
        return;
    if (ball_y + BALL_SIZE < paddle_y)
        return;
    if (ball_y > paddle_y + PADDLE_H)
        return;
    if (!ball_going_into_paddle)
        return;

    ball_vx = -ball_vx;
    // Spin: where on the paddle did we hit, normalised to [-1, +1].
    float ball_cy = ball_y + BALL_SIZE / 2.0f;
    float paddle_cy = paddle_y + PADDLE_H / 2.0f;
    float offset = (ball_cy - paddle_cy) / (PADDLE_H / 2.0f);
    if (offset < -1.0f)
        offset = -1.0f;
    if (offset > 1.0f)
        offset = 1.0f;
    ball_vy = offset * BALL_SPEED_Y * 1.5f;
}

static void simulate_ball() {
    ball_x += ball_vx;
    ball_y += ball_vy;

    // Top / bottom walls.
    if (ball_y < 0.0f) {
        ball_y = 0.0f;
        ball_vy = -ball_vy;
    }
    if (ball_y > CANVAS_H - BALL_SIZE) {
        ball_y = CANVAS_H - BALL_SIZE;
        ball_vy = -ball_vy;
    }

    // Paddles.
    try_paddle_collide(PADDLE_X_LEFT, left_y, ball_vx < 0.0f);
    try_paddle_collide(PADDLE_X_RIGHT, right_y, ball_vx > 0.0f);

    // Scoring: ball passed a goal line. Loser gets the next serve.
    if (ball_x + BALL_SIZE < 0.0f) {
        right_score++;
        reset_ball(-1);
    } else if (ball_x > CANVAS_W) {
        left_score++;
        reset_ball(+1);
    }
}

// Build & send a state snapshot from the host.
//   "S bx by ly ry ls rs"
static void send_state() {
    int pos = 0;
    send_buf[pos++] = 'S';
    send_buf[pos++] = ' ';
    pos += int_to_str((int)ball_x, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)ball_y, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)left_y, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)right_y, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str(left_score, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str(right_score, send_buf + pos);
    peer_send(send_buf, pos);
}

// Build & send the guest's paddle position to the host.
//   "P y"
static void send_paddle() {
    int pos = 0;
    send_buf[pos++] = 'P';
    send_buf[pos++] = ' ';
    pos += int_to_str((int)right_y, send_buf + pos);
    peer_send(send_buf, pos);
}

// ── Rendering ─────────────────────────────────────────────────────────────
static void render() {
    clear_canvas();

    // Dashed center line (just rectangles).
    constexpr float DASH_H = 16.0f;
    constexpr float DASH_W = 4.0f;
    constexpr float GAP = 12.0f;
    for (float y = 0.0f; y < CANVAS_H; y += DASH_H + GAP) {
        fill_rect(CANVAS_W / 2.0f - DASH_W / 2.0f, y, DASH_W, DASH_H, 80, 80,
                  80);
    }

    // Paddles + ball.
    fill_rect(PADDLE_X_LEFT, left_y, PADDLE_W, PADDLE_H, 255, 255, 255);
    fill_rect(PADDLE_X_RIGHT, right_y, PADDLE_W, PADDLE_H, 255, 255, 255);
    fill_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE, 255, 255, 255);

    // Scores. fill_text uses the canvas baseline so y is the bottom of the
    // glyph.
    char buf[8];
    int n;
    constexpr int SCORE_SIZE = 56;

    n = int_to_str(left_score, buf);
    fill_text(CANVAS_W / 2.0f - 80.0f, 70.0f, buf, n, SCORE_SIZE, 200, 200,
              200);

    n = int_to_str(right_score, buf);
    fill_text(CANVAS_W / 2.0f + 50.0f, 70.0f, buf, n, SCORE_SIZE, 200, 200,
              200);
}

// ── Exports ───────────────────────────────────────────────────────────────
extern "C" {

__attribute__((export_name("get_msg_buf"))) int get_msg_buf() {
    return (int)(long)msg_buf;
}
__attribute__((export_name("get_msg_buf_size"))) int get_msg_buf_size() {
    return (int)sizeof(msg_buf);
}

__attribute__((export_name("init"))) void init() {
    in_game = false;
    role = 0;
    serve_seed = 0;
    left_y = right_y = (CANVAS_H - PADDLE_H) / 2.0f;
    left_score = right_score = 0;
    reset_ball(+1);
}

__attribute__((export_name("start_game"))) void start_game(int r) {
    role = r;
    left_y = right_y = (CANVAS_H - PADDLE_H) / 2.0f;
    left_score = right_score = 0;
    reset_ball((r == 0) ? +1 : -1);
    in_game = true;
}

__attribute__((export_name("stop_game"))) void stop_game() { in_game = false; }

__attribute__((export_name("tick"))) void tick() {
    if (!in_game)
        return;

    move_local_paddle();

    if (role == 0) {
        // Host: authoritative ball physics. Guest paddle was updated from
        // the most recent `P y` we received in on_peer_message.
        simulate_ball();
        send_state();
    } else {
        // Guest: just publish our paddle position; host's `S ...` will
        // overwrite ball/score/left_y next frame.
        send_paddle();
    }

    render();
}

// Peer wrote `len` bytes into msg_buf.
//   On host: expect `P <y>`.
//   On guest: expect `S bx by ly ry ls rs`.
__attribute__((export_name("on_peer_message"))) void on_peer_message(int len) {
    if (len < 2)
        return;

    if (role == 0 && msg_buf[0] == 'P') {
        // "P <y>"
        int i = skip_sep(msg_buf, len, 1);
        int y;
        parse_int(msg_buf, len, i, &y);
        right_y = clampf((float)y, 0.0f, CANVAS_H - PADDLE_H);
    } else if (role == 1 && msg_buf[0] == 'S') {
        // "S bx by ly ry ls rs" — overwrite all authoritative state. Our
        // local right_y was sent to the host last frame, so the value we
        // get back here is at most one round-trip stale; close enough.
        int vals[6];
        int i = 1;
        for (int k = 0; k < 6; k++) {
            i = skip_sep(msg_buf, len, i);
            i = parse_int(msg_buf, len, i, &vals[k]);
        }
        ball_x = (float)vals[0];
        ball_y = (float)vals[1];
        left_y = (float)vals[2];
        right_y = (float)vals[3];
        left_score = vals[4];
        right_score = vals[5];
    }
}

} // extern "C"
