// Loads pong.wasm, provides imports the wasm module needs (canvas
// drawing primitives + console logging), then drives `tick()` via
// requestAnimationFrame.

const canvas = document.getElementById('canvas');
const ctx = canvas.getContext('2d');

// These functions form the entire host->guest API. The wasm module
// declares matching `import_name` attributes for each.
const imports = {
    env: {
        clear_canvas: () => {
            ctx.fillStyle = '#000';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
        },
        fill_rect: (x, y, w, h, r, g, b) => {
            ctx.fillStyle = `rgb(${r},${g},${b})`;
            ctx.fillRect(x, y, w, h);
        },
        console_log_int: (v) => {
            console.log('wasm:', v);
        },
    },
};

const { instance } = await WebAssembly.instantiateStreaming(
    fetch('pong.wasm'),
    imports,
);

const { init, tick } = instance.exports;

init();

function frame() {
    tick();
    requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
