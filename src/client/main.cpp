// Pong client engine.
//
// Authority model: host = left paddle (P1) + ball/scoring authority.
// Guest = right paddle (P2). Each tick:
//   - Both peers read local input via paddle_input_{x,y}() and move their
//     own paddle (clamped). The guest's paddle move is therefore locally
//     responsive even before the network catches up.
//   - Guest sends `P x y` (its paddle's top-left, integer pixels).
//   - Host receives `P x y`, mirrors the guest's paddle, computes the
//     guest's vx from the x delta, simulates the ball + collisions +
//     scoring, and broadcasts `S bx by lx ly rx ry ls rs`.
//   - Guest receives `S ...` and overwrites everything except its own
//     paddle (kept locally to avoid round-trip snap-back).
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
// Each returns -1, 0, or +1 based on currently-held keys.
__attribute__((import_name("paddle_input_x"))) int
paddle_input_x(); // - = left,  + = right
__attribute__((import_name("paddle_input_y"))) int
paddle_input_y(); // - = up,    + = down

// ── Peer data channel ─────────────────────────────────────────────────────
__attribute__((import_name("peer_send"))) void peer_send(const char* ptr,
                                                         int len);
}

// ── Constants ─────────────────────────────────────────────────────────────
constexpr float CANVAS_W = 800.0f;
constexpr float CANVAS_H = 600.0f;
constexpr float HALF_W = CANVAS_W / 2.0f;
constexpr float BALL_SIZE = 14.0f;
constexpr float PADDLE_W = 12.0f;
constexpr float PADDLE_H = 90.0f;
constexpr float PADDLE_INSET =
    24.0f; // distance from canvas edge to home position
constexpr float LEFT_HOME_X = PADDLE_INSET;
constexpr float RIGHT_HOME_X = CANVAS_W - PADDLE_INSET - PADDLE_W;
constexpr float PADDLE_SPEED_Y = 6.0f;
constexpr float PADDLE_SPEED_X =
    4.0f; // a touch slower so lunges feel deliberate
// Each paddle is restricted to its half of the field.
constexpr float LEFT_X_MIN = 0.0f;
constexpr float LEFT_X_MAX = HALF_W - PADDLE_W;
constexpr float RIGHT_X_MIN = HALF_W;
constexpr float RIGHT_X_MAX = CANVAS_W - PADDLE_W;
constexpr float BALL_SPEED_X = 5.0f;
constexpr float BALL_SPEED_Y = 3.0f;
constexpr float MAX_BALL_VX = 14.0f; // ceiling so the ball can't run away
constexpr float PADDLE_BOOST =
    0.6f; // fraction of paddle vx folded into ball vx

// ── Application state ─────────────────────────────────────────────────────
static bool in_game;
static int
    role; // 0 = host (left), 1 = guest (right). Mirrors web/src/types.ts Role.
static int
    serve_seed; // ticks-since-init; provides cheap variance for ball serves.

static float ball_x, ball_y, ball_vx, ball_vy;
static float left_x, left_y, right_x, right_y;
// Paddle x-velocity captured from the most recent move (this frame for our
// own paddle; from x-deltas of received `P` frames for the peer's paddle).
// Folded into ball.vx on collision so lunging gives the ball a kick.
static float left_vx, right_vx;
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

// Parse signed int starting at index `start`, stops at non-digit. Writes
// the parsed value to *out and returns the index *just past* the last
// consumed char.
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

// Skip any non-digit/non-sign chars at index i (typically a single space).
static int skip_sep(const char* s, int len, int i) {
    while (i < len && s[i] != '-' && (s[i] < '0' || s[i] > '9'))
        i++;
    return i;
}

// ── Game logic ────────────────────────────────────────────────────────────
static float clampf(float v, float lo, float hi) {
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

static void reset_ball(int direction) {
    ball_x = HALF_W - BALL_SIZE / 2.0f;
    ball_y = CANVAS_H / 2.0f - BALL_SIZE / 2.0f;
    ball_vx = direction * BALL_SPEED_X;
    // Cheap variance: alternate vy sign with each serve.
    ball_vy = (serve_seed & 1) ? BALL_SPEED_Y : -BALL_SPEED_Y;
    serve_seed++;
}

static void move_local_paddle() {
    int dx = paddle_input_x();
    int dy = paddle_input_y();

    float* my_x = (role == 0) ? &left_x : &right_x;
    float* my_y = (role == 0) ? &left_y : &right_y;
    float* my_vx = (role == 0) ? &left_vx : &right_vx;

    *my_y = clampf(*my_y + dy * PADDLE_SPEED_Y, 0.0f, CANVAS_H - PADDLE_H);

    float min_x = (role == 0) ? LEFT_X_MIN : RIGHT_X_MIN;
    float max_x = (role == 0) ? LEFT_X_MAX : RIGHT_X_MAX;
    float new_x = clampf(*my_x + dx * PADDLE_SPEED_X, min_x, max_x);
    *my_vx = new_x - *my_x; // actual delta after clamping
    *my_x = new_x;
}

// AABB hit test between ball and a paddle. Reflects ball_vx, biases ball_vy
// toward the offset from paddle center so players can angle returns, and
// folds in PADDLE_BOOST * paddle_vx so lunging into the ball speeds it up.
// Only flips ball_vx if it's heading INTO the paddle to avoid double-bounce.
static void try_paddle_collide(float paddle_x, float paddle_y, float paddle_vx,
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

    // Sign convention: after the flip, ball_vx points away from this paddle.
    // "Lunging toward the ball" = paddle_vx in that same direction (left
    // paddle moving right; right paddle moving left), so simple addition
    // adds energy on a true lunge and bleeds energy on a retreat.
    ball_vx += paddle_vx * PADDLE_BOOST;
    if (ball_vx > MAX_BALL_VX)
        ball_vx = MAX_BALL_VX;
    if (ball_vx < -MAX_BALL_VX)
        ball_vx = -MAX_BALL_VX;

    // Spin from where on the paddle we hit, normalised to [-1, +1].
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
    try_paddle_collide(left_x, left_y, left_vx, ball_vx < 0.0f);
    try_paddle_collide(right_x, right_y, right_vx, ball_vx > 0.0f);

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
//   "S bx by lx ly rx ry ls rs"
static void send_state() {
    int pos = 0;
    send_buf[pos++] = 'S';
    send_buf[pos++] = ' ';
    pos += int_to_str((int)ball_x, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)ball_y, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)left_x, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)left_y, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)right_x, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str((int)right_y, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str(left_score, send_buf + pos);
    send_buf[pos++] = ' ';
    pos += int_to_str(right_score, send_buf + pos);
    peer_send(send_buf, pos);
}

// Build & send the guest's paddle position to the host.
//   "P x y"
static void send_paddle() {
    int pos = 0;
    send_buf[pos++] = 'P';
    send_buf[pos++] = ' ';
    pos += int_to_str((int)right_x, send_buf + pos);
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
        fill_rect(HALF_W - DASH_W / 2.0f, y, DASH_W, DASH_H, 80, 80, 80);
    }

    // Paddles + ball.
    fill_rect(left_x, left_y, PADDLE_W, PADDLE_H, 255, 255, 255);
    fill_rect(right_x, right_y, PADDLE_W, PADDLE_H, 255, 255, 255);
    fill_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE, 255, 255, 255);

    // Scores. fill_text uses the canvas baseline so y is the bottom of the
    // glyph.
    char buf[8];
    int n;
    constexpr int SCORE_SIZE = 56;

    n = int_to_str(left_score, buf);
    fill_text(HALF_W - 80.0f, 70.0f, buf, n, SCORE_SIZE, 200, 200, 200);

    n = int_to_str(right_score, buf);
    fill_text(HALF_W + 50.0f, 70.0f, buf, n, SCORE_SIZE, 200, 200, 200);
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
    left_x = LEFT_HOME_X;
    right_x = RIGHT_HOME_X;
    left_y = right_y = (CANVAS_H - PADDLE_H) / 2.0f;
    left_vx = right_vx = 0.0f;
    left_score = right_score = 0;
    reset_ball(+1);
}

__attribute__((export_name("start_game"))) void start_game(int r) {
    role = r;
    left_x = LEFT_HOME_X;
    right_x = RIGHT_HOME_X;
    left_y = right_y = (CANVAS_H - PADDLE_H) / 2.0f;
    left_vx = right_vx = 0.0f;
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
        // the most recent `P x y` we received in on_peer_message.
        simulate_ball();
        send_state();
    } else {
        // Guest: just publish our paddle position; host's `S ...` will
        // overwrite ball / left paddle / scores next frame.
        send_paddle();
    }

    render();
}

// Peer wrote `len` bytes into msg_buf.
//   On host: expect `P x y`.
//   On guest: expect `S bx by lx ly rx ry ls rs`.
__attribute__((export_name("on_peer_message"))) void on_peer_message(int len) {
    if (len < 2)
        return;

    if (role == 0 && msg_buf[0] == 'P') {
        // "P x y". Compute the guest's vx from the delta vs. last reported
        // x so the host can apply PADDLE_BOOST during collision.
        int i = skip_sep(msg_buf, len, 1);
        int x;
        i = parse_int(msg_buf, len, i, &x);
        i = skip_sep(msg_buf, len, i);
        int y;
        parse_int(msg_buf, len, i, &y);

        float new_x = clampf((float)x, RIGHT_X_MIN, RIGHT_X_MAX);
        right_vx = new_x - right_x;
        right_x = new_x;
        right_y = clampf((float)y, 0.0f, CANVAS_H - PADDLE_H);
    } else if (role == 1 && msg_buf[0] == 'S') {
        // "S bx by lx ly rx ry ls rs". We deliberately ignore vals[4]/vals[5]
        // (our own paddle) so local input stays responsive; the host's
        // mirror of us is at most one RTT stale, so adopting it would cause
        // a visible snap-back at non-trivial latencies.
        int vals[8];
        int i = 1;
        for (int k = 0; k < 8; k++) {
            i = skip_sep(msg_buf, len, i);
            i = parse_int(msg_buf, len, i, &vals[k]);
        }
        ball_x = (float)vals[0];
        ball_y = (float)vals[1];
        left_x = (float)vals[2];
        left_y = (float)vals[3];
        // skip vals[4], vals[5] — our own paddle
        left_score = vals[6];
        right_score = vals[7];
    }
}

} // extern "C"
