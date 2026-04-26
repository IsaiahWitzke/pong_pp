extern "C" {
// ── Canvas ────────────────────────────────────────────────────────────────
__attribute__((import_name("clear_canvas"))) void clear_canvas();
__attribute__((import_name("fill_rect")))    void fill_rect(float x, float y, float w, float h, int r, int g, int b);
__attribute__((import_name("console_log_int"))) void console_log_int(int v);

// ── WebSocket bridge ──────────────────────────────────────────────────────
__attribute__((import_name("ws_connect"))) void ws_connect();
__attribute__((import_name("ws_send")))    void ws_send(const char* ptr, int len);
__attribute__((import_name("ws_close")))   void ws_close();

// ── DOM bridge ────────────────────────────────────────────────────────────
__attribute__((import_name("dom_show_screen")))         void dom_show_screen(int id); // 0=menu 1=waiting 2=game
__attribute__((import_name("dom_set_room_list")))        void dom_set_room_list(const char* ptr, int len);
__attribute__((import_name("dom_set_waiting_code")))     void dom_set_waiting_code(const char* ptr, int len);
__attribute__((import_name("dom_append_chat")))          void dom_append_chat(int who, const char* ptr, int len); // 0=me 1=peer
__attribute__((import_name("dom_show_error")))           void dom_show_error(const char* ptr, int len);
__attribute__((import_name("dom_set_buttons_disabled"))) void dom_set_buttons_disabled(int d);
}

// ── Constants ─────────────────────────────────────────────────────────────
constexpr float CANVAS_W  = 800.0f;
constexpr float CANVAS_H  = 600.0f;
constexpr float BALL_SIZE = 16.0f;

// ── Application state ─────────────────────────────────────────────────────
enum class Screen { Menu = 0, Waiting = 1, Game = 2 };
static Screen current_screen;
static float  ball_x, ball_y, ball_vx, ball_vy;
static bool   reconnecting;

// JS writes incoming WS messages / user input into msg_buf before calling exports.
// C++ builds outgoing WS messages in send_buf.
static char msg_buf[512];
static char send_buf[256];
static char aux_buf[256];

// ── String helpers (no stdlib) ────────────────────────────────────────────
static int slen(const char* s) {
    int n = 0; while (s[n]) n++; return n;
}

static bool seq(const char* a, int alen, const char* b, int blen) {
    if (alen != blen) return false;
    for (int i = 0; i < alen; i++) if (a[i] != b[i]) return false;
    return true;
}

static int scopy(char* dst, const char* src, int n) {
    for (int i = 0; i < n; i++) dst[i] = src[i]; return n;
}

static bool starts_with(const char* s, int slen_, const char* p, int plen) {
    if (slen_ < plen) return false;
    for (int i = 0; i < plen; i++) if (s[i] != p[i]) return false;
    return true;
}

// ── Message sending ───────────────────────────────────────────────────────
static void send_raw(const char* msg, int len) { ws_send(msg, len); }

static void send_verb(const char* verb, const char* body, int blen) {
    int pos = scopy(send_buf, verb, slen(verb));
    if (body && blen > 0) {
        send_buf[pos++] = ' ';
        pos += scopy(send_buf + pos, body, blen);
    }
    ws_send(send_buf, pos);
}

// ── Protocol handlers ─────────────────────────────────────────────────────
static void handle_rooms(const char* body, int blen) {
    if (current_screen != Screen::Menu) return;
    dom_set_room_list(body ? body : "", blen);
}

static void handle_created(const char* code, int len) {
    current_screen = Screen::Waiting;
    dom_show_screen(1);
    dom_set_waiting_code(code, len);
}

static void handle_ready(const char* /*role*/, int /*rlen*/) {
    ball_x = CANVAS_W / 2.0f; ball_y = CANVAS_H / 2.0f;
    ball_vx = 3.5f;           ball_vy = 2.5f;
    current_screen = Screen::Game;
    dom_show_screen(2);
}

static void handle_relay(const char* body, int blen) {
    static const char kChat[] = "chat:";
    if (starts_with(body, blen, kChat, sizeof(kChat) - 1))
        dom_append_chat(1, body + sizeof(kChat) - 1, blen - (int)(sizeof(kChat) - 1));
}

static void handle_peer_left() {
    current_screen = Screen::Menu;
    dom_show_screen(0);
    dom_set_buttons_disabled(0);
    static const char kMsg[]  = "opponent left the game";
    static const char kList[] = "LIST";
    dom_show_error(kMsg, sizeof(kMsg) - 1);
    send_raw(kList, sizeof(kList) - 1);
}

static void handle_error(const char* reason, int rlen) {
    if (seq(reason, rlen, "join_failed", 11)) {
        static const char kMsg[] = "room not found or already full";
        dom_show_error(kMsg, sizeof(kMsg) - 1);
    } else if (seq(reason, rlen, "could_not_allocate_room", 23)) {
        static const char kMsg[] = "could not create room";
        dom_show_error(kMsg, sizeof(kMsg) - 1);
    } else {
        static const char kMsg[] = "server error";
        dom_show_error(kMsg, sizeof(kMsg) - 1);
    }
    dom_set_buttons_disabled(0);
}

// ── Exports ───────────────────────────────────────────────────────────────
extern "C" {

// JS writes a string into msg_buf before calling an export that consumes it.
__attribute__((export_name("get_msg_buf")))
int get_msg_buf() { return (int)(long)msg_buf; }

__attribute__((export_name("get_msg_buf_size")))
int get_msg_buf_size() { return (int)sizeof(msg_buf); }

__attribute__((export_name("init")))
void init() {
    current_screen = Screen::Menu;
    ball_x = CANVAS_W / 2.0f; ball_y = CANVAS_H / 2.0f;
    ball_vx = 3.5f;           ball_vy = 2.5f;
    reconnecting = false;
    dom_show_screen(0);
    ws_connect();
}

// Returns early when not in game; JS always calls this each rAF frame.
__attribute__((export_name("tick")))
void tick() {
    if (current_screen != Screen::Game) return;
    ball_x += ball_vx;
    ball_y += ball_vy;
    if (ball_x < 0.0f || ball_x > CANVAS_W - BALL_SIZE) ball_vx = -ball_vx;
    if (ball_y < 0.0f || ball_y > CANVAS_H - BALL_SIZE) ball_vy = -ball_vy;
    clear_canvas();
    fill_rect(ball_x, ball_y, BALL_SIZE, BALL_SIZE, 255, 255, 255);
}

__attribute__((export_name("on_ws_open")))
void on_ws_open() {
    dom_set_buttons_disabled(0);
    static const char kList[] = "LIST";
    send_raw(kList, sizeof(kList) - 1);
}

__attribute__((export_name("on_ws_message")))
void on_ws_message(int len) {
    int sp = -1;
    for (int i = 0; i < len; i++) { if (msg_buf[i] == ' ') { sp = i; break; } }

    const char* verb = msg_buf;
    int          vlen = (sp == -1) ? len : sp;
    const char* body  = (sp == -1) ? nullptr : msg_buf + sp + 1;
    int          blen  = (sp == -1) ? 0 : len - sp - 1;

    if      (seq(verb, vlen, "ROOMS",     5)) handle_rooms(body, blen);
    else if (seq(verb, vlen, "CREATED",   7)) handle_created(body, blen);
    else if (seq(verb, vlen, "READY",     5)) handle_ready(body, blen);
    else if (seq(verb, vlen, "RELAY",     5)) handle_relay(body, blen);
    else if (seq(verb, vlen, "PEER_LEFT", 9)) handle_peer_left();
    else if (seq(verb, vlen, "ERROR",     5)) handle_error(body, blen);
}

__attribute__((export_name("on_ws_close")))
void on_ws_close() {
    if (reconnecting) { reconnecting = false; ws_connect(); return; }
    static const char kMsg[] = "disconnected — reload to reconnect";
    dom_show_error(kMsg, sizeof(kMsg) - 1);
}

__attribute__((export_name("on_ws_error")))
void on_ws_error() {
    static const char kMsg[] = "connection error — reload to reconnect";
    dom_show_error(kMsg, sizeof(kMsg) - 1);
}

__attribute__((export_name("on_btn_host")))
void on_btn_host() {
    dom_set_buttons_disabled(1);
    static const char kCreate[] = "CREATE";
    send_raw(kCreate, sizeof(kCreate) - 1);
}

__attribute__((export_name("on_btn_join")))
void on_btn_join(int len) {
    if (len == 0) {
        static const char kMsg[] = "enter a room code";
        dom_show_error(kMsg, sizeof(kMsg) - 1);
        return;
    }
    for (int i = 0; i < len; i++) {
        if (msg_buf[i] < '0' || msg_buf[i] > '9') {
            static const char kMsg[] = "room code must be a number";
            dom_show_error(kMsg, sizeof(kMsg) - 1);
            return;
        }
    }
    dom_set_buttons_disabled(1);
    send_verb("JOIN", msg_buf, len);
}

__attribute__((export_name("on_btn_cancel")))
void on_btn_cancel() {
    reconnecting = true;
    dom_set_buttons_disabled(1);
    ws_close();
    current_screen = Screen::Menu;
    dom_show_screen(0);
}

__attribute__((export_name("on_chat_send")))
void on_chat_send(int len) {
    if (len == 0 || current_screen != Screen::Game) return;
    dom_append_chat(0, msg_buf, len);
    static const char kPrefix[] = "RELAY chat:";
    int pos = scopy(send_buf, kPrefix, sizeof(kPrefix) - 1);
    pos += scopy(send_buf + pos, msg_buf, len);
    ws_send(send_buf, pos);
}

} // extern "C"
