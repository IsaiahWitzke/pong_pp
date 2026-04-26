// Thin JS shim: instantiates pong.wasm, bridges WebSocket + DOM APIs to WASM
// imports, and drives the rAF loop. All application logic lives in C++.

const canvas = document.getElementById('canvas');
const ctx    = canvas.getContext('2d');

let instance;
let ws;

// Read len bytes from WASM linear memory at ptr, decode as UTF-8.
function readMem(ptr, len) {
    return new TextDecoder().decode(
        new Uint8Array(instance.exports.memory.buffer, ptr, len));
}

// Encode str as UTF-8 and write into C++ msg_buf. Returns bytes written.
function writeMsg(str) {
    const enc = new TextEncoder().encode(str);
    const ptr = instance.exports.get_msg_buf();
    const cap = instance.exports.get_msg_buf_size();
    const len = Math.min(enc.length, cap - 1);
    new Uint8Array(instance.exports.memory.buffer, ptr, len).set(enc.subarray(0, len));
    return len;
}

const imports = {
    env: {
        // ── Canvas ──────────────────────────────────────────────────────
        clear_canvas: () => {
            ctx.fillStyle = '#000';
            ctx.fillRect(0, 0, canvas.width, canvas.height);
        },
        fill_rect: (x, y, w, h, r, g, b) => {
            ctx.fillStyle = `rgb(${r},${g},${b})`;
            ctx.fillRect(x, y, w, h);
        },
        console_log_int: (v) => console.log('wasm:', v),

        // ── WebSocket bridge ─────────────────────────────────────────────
        ws_connect: () => {
            ws = new WebSocket('ws://localhost:9000');
            ws.addEventListener('open',    () => instance.exports.on_ws_open());
            ws.addEventListener('close',   () => instance.exports.on_ws_close());
            ws.addEventListener('error',   () => instance.exports.on_ws_error());
            ws.addEventListener('message', (e) => {
                const len = writeMsg(e.data);
                instance.exports.on_ws_message(len);
            });
        },
        ws_send:  (ptr, len) => ws.send(readMem(ptr, len)),
        ws_close: () => ws && ws.close(),

        // ── DOM bridge ───────────────────────────────────────────────────
        dom_show_screen: (id) => {
            ['screen-menu', 'screen-waiting', 'screen-game'].forEach((name, i) =>
                document.getElementById(name).classList.toggle('active', i === id));
        },
        dom_set_room_list: (ptr, len) => {
            const el = document.getElementById('room-list');
            el.innerHTML = '';
            const text = readMem(ptr, len).trim();
            if (!text) {
                const span = document.createElement('span');
                span.className = 'empty';
                span.textContent = 'no open rooms';
                el.appendChild(span);
                return;
            }
            text.split(' ').forEach(code => {
                const btn = document.createElement('button');
                btn.className = 'btn join-room-btn';
                btn.textContent = `JOIN ROOM ${code}`;
                btn.addEventListener('click', () => {
                    const n = writeMsg(code);
                    instance.exports.on_btn_join(n);
                });
                el.appendChild(btn);
            });
        },
        dom_set_waiting_code: (ptr, len) => {
            document.getElementById('waiting-code').textContent = readMem(ptr, len);
        },
        dom_append_chat: (who, ptr, len) => {
            const div = document.createElement('div');
            div.className = who === 0 ? 'msg-me' : 'msg-peer';
            div.textContent = (who === 0 ? 'you: ' : 'peer: ') + readMem(ptr, len);
            const log = document.getElementById('chat-log');
            log.appendChild(div);
            log.scrollTop = log.scrollHeight;
        },
        dom_show_error: (ptr, len) => {
            const b = document.getElementById('error-banner');
            b.textContent = readMem(ptr, len);
            b.classList.add('visible');
            setTimeout(() => b.classList.remove('visible'), 4000);
        },
        dom_set_buttons_disabled: (d) => {
            document.getElementById('btn-host').disabled = !!d;
            document.getElementById('btn-join').disabled = !!d;
            document.querySelectorAll('.join-room-btn').forEach(b => { b.disabled = !!d; });
        },
    },
};

const result = await WebAssembly.instantiateStreaming(fetch('pong.wasm'), imports);
instance = result.instance;
instance.exports.init();

// ── DOM event wiring ──────────────────────────────────────────────────────
document.getElementById('btn-host').addEventListener('click', () =>
    instance.exports.on_btn_host());

document.getElementById('btn-cancel').addEventListener('click', () =>
    instance.exports.on_btn_cancel());

function sendJoin() {
    const len = writeMsg(document.getElementById('join-input').value);
    instance.exports.on_btn_join(len);
}
document.getElementById('btn-join').addEventListener('click', sendJoin);
document.getElementById('join-input').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') sendJoin();
});
document.getElementById('join-input').addEventListener('input', (e) => {
    e.target.value = e.target.value.replace(/[^0-9]/g, '');
});

function sendChat() {
    const input = document.getElementById('chat-input');
    const len = writeMsg(input.value);
    instance.exports.on_chat_send(len);
    input.value = '';
}
document.getElementById('chat-send').addEventListener('click', sendChat);
document.getElementById('chat-input').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') sendChat();
});

// ── rAF loop — always ticks; C++ returns early when not in game ───────────
function frame() {
    instance.exports.tick();
    requestAnimationFrame(frame);
}
requestAnimationFrame(frame);
