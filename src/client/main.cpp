// pong_pp — hello world starter

extern "C" {
    __attribute__((import_name("clear_canvas")))
    void clear_canvas();

    __attribute__((import_name("fill_rect")))
    void fill_rect(float x, float y, float w, float h,
                   int r, int g, int b);

    __attribute__((import_name("console_log_int")))
    void console_log_int(int v);
}

constexpr float CANVAS_W = 800.0f;
constexpr float CANVAS_H = 600.0f;

struct State {
    float ball_x  = CANVAS_W / 2.0f;
    float ball_y  = CANVAS_H / 2.0f;
    float ball_vx = 3.5f;
    float ball_vy = 2.5f;
    int   frame   = 0;
};
static State g;

extern "C" {
    __attribute__((export_name("init")))
    void init() {
        console_log_int(42);
    }

    // Called every animation frame from JS.
    __attribute__((export_name("tick")))
    void tick() {
        g.frame++;
        g.ball_x += g.ball_vx;
        g.ball_y += g.ball_vy;

        // bounce off walls
        if (g.ball_x < 0.0f || g.ball_x > CANVAS_W - 16.0f) g.ball_vx = -g.ball_vx;
        if (g.ball_y < 0.0f || g.ball_y > CANVAS_H - 16.0f) g.ball_vy = -g.ball_vy;

        clear_canvas();
        fill_rect(g.ball_x, g.ball_y, 16.0f, 16.0f, 255, 255, 255);
    }
}
