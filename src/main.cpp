// pong_pp — hello world starter
//
// Compiled with raw clang (--target=wasm32, -nostdlib). No Emscripten,
// no SDL2, no libc. Drawing primitives and console logging are imported
// from JavaScript; the game loop is driven by requestAnimationFrame on
// the JS side, which calls our exported `tick()` each frame.

extern "C" {
    // ---- imported from JS ----
    __attribute__((import_name("clear_canvas")))
    void clear_canvas();

    __attribute__((import_name("fill_rect")))
    void fill_rect(float x, float y, float w, float h,
                   int r, int g, int b);

    __attribute__((import_name("console_log_int")))
    void console_log_int(int v);
}

// ---- canvas dimensions (must match loader.js) ----
constexpr float CANVAS_W = 800.0f;
constexpr float CANVAS_H = 600.0f;

// ---- game state ----
//
// We can't use `new` or std::vector without a libc++, so we keep state
// as file-scope globals. For Pong this is plenty — there's a fixed,
// known set of entities.
struct State {
    float ball_x  = CANVAS_W / 2.0f;
    float ball_y  = CANVAS_H / 2.0f;
    float ball_vx = 3.5f;
    float ball_vy = 2.5f;
    int   frame   = 0;
};
static State g;

// ---- exports ----
extern "C" {
    // Called once after the wasm module is instantiated.
    __attribute__((export_name("init")))
    void init() {
        // Prove the import bridge works: should print "wasm: 42" in the
        // browser console.
        console_log_int(42);
    }

    // Called every animation frame from JS.
    __attribute__((export_name("tick")))
    void tick() {
        // ---- update ----
        g.frame++;
        g.ball_x += g.ball_vx;
        g.ball_y += g.ball_vy;

        // bounce off walls
        if (g.ball_x < 0.0f || g.ball_x > CANVAS_W - 16.0f) g.ball_vx = -g.ball_vx;
        if (g.ball_y < 0.0f || g.ball_y > CANVAS_H - 16.0f) g.ball_vy = -g.ball_vy;

        // ---- render ----
        clear_canvas();
        fill_rect(g.ball_x, g.ball_y, 16.0f, 16.0f, 255, 255, 255);
    }
}
