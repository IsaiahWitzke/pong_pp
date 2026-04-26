Two parts. I'll redo the handshake first because it tells you what the server has to relay, then walk through the server itself.

## The handshake, step by step

Setup: two browsers (peer A = host, peer B = guest), one signaling server they both connect to over WebSocket.

### Step 0: Both peers connect to the signaling server

Plain WebSocket. Server pairs them - A becomes host, B becomes guest, server creates an internal "session" linking the two sockets. From here on, anything A sends through the WebSocket gets forwarded byte-for-byte to B's WebSocket, and vice versa. **The server never reads the payloads.**

### Step 1: A creates a peer connection + data channel

```js
const pc = new RTCPeerConnection({ iceServers: [{ urls: 'stun:stun.l.google.com:19302' }] });
const dc = pc.createDataChannel('game', { ordered: false, maxRetransmits: 0 });
```
Just creating the PC kicks off ICE: the browser starts asking "what addresses can people reach me at?" - local interfaces, plus a STUN query to find the public IP/port the NAT mapped it to. Each result becomes an *ICE candidate*.

### Step 2: A makes an offer, sends it through signaling

```js
const offer = await pc.createOffer();
await pc.setLocalDescription(offer);
ws.send(JSON.stringify({ kind: 'relay', payload: { type: 'offer', sdp: offer.sdp } }));
```
The offer is an SDP text blob. The parts that matter conceptually:

```
m=application 9 UDP/DTLS/SCTP webrtc-datachannel
a=fingerprint:sha-256 9C:1F:23:...
a=ice-ufrag:F7gI
a=ice-pwd:x9cml/YzichV2+XlhiMu8g
```
- `m=application` - "I want a data channel, transported as SCTP over DTLS over UDP."
- `a=fingerprint` - "Here's my DTLS public key fingerprint. If you end up encrypting to a different fingerprint, you're talking to a MITM."
- `a=ice-ufrag` / `a=ice-pwd` - username + password used to authenticate the candidate-pair connectivity probes (so a random attacker on the network can't masquerade).

Server forwards verbatim to B.

### Step 3: B applies the offer, makes an answer, sends it back

```js
await pc.setRemoteDescription({ type: 'offer', sdp: msg.sdp });
const answer = await pc.createAnswer();
await pc.setLocalDescription(answer);
ws.send(JSON.stringify({ kind: 'relay', payload: { type: 'answer', sdp: answer.sdp } }));
```
Answer is structurally the same kind of SDP blob, with B's fingerprint and ICE credentials. A applies it (`setRemoteDescription`).

After step 3, both sides know each other's *intent and crypto identity*. They still don't have a network path.

### Step 4: ICE candidates trickle through (in parallel with steps 2–3)

Each side emits candidates as it discovers them, asynchronously:

```js
pc.onicecandidate = (e) => {
    if (e.candidate) ws.send(JSON.stringify({ kind: 'relay', payload: { type: 'ice', candidate: e.candidate } }));
};
```
A candidate looks like `candidate:842163049 1 udp 1677729535 73.42.1.7 54321 typ srflx ...` - "you can reach me at 73.42.1.7:54321 over UDP, this came from STUN reflection." Receiving side does `pc.addIceCandidate(msg.candidate)`.

Both browsers now have lists of (my-addresses, your-addresses). Their ICE agents try every pair: send a tiny STUN binding request to the remote address; if a response comes back, that pair works. They pick the best working pair (lowest latency, prefer direct over relayed).

### Step 5: DTLS + SCTP come up automatically

Over the chosen candidate pair, the browsers do a DTLS handshake. Each side verifies the peer's DTLS certificate matches the fingerprint exchanged in steps 2–3. **This is the part that protects you from a malicious signaling server**: the server saw every SDP and ICE message, but it never had either peer's DTLS private key, so it can't impersonate either side. Then SCTP comes up on top of DTLS, and that gives the data channel its framing.

### Step 6: `dc.onopen` fires on both sides

The signaling server is now irrelevant. Gameplay traffic flows peer-to-peer over the data channel.

### Recap: what crossed the signaling server

- 1 `offer` (A → B)
- 1 `answer` (B → A)
- ~2–10 `ice` candidates each direction
- All payloads opaque to the server

That's it. Total signaling-server lifetime per match: roughly one second.

## How to build the server

The server is dumber than the handshake makes it sound. Five things:

### 1. Listen on a TCP port

Plain BSD sockets. ~30 lines:

```cpp
int lfd = socket(AF_INET, SOCK_STREAM, 0);
int yes = 1; setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(9000); addr.sin_addr.s_addr = INADDR_ANY;
bind(lfd, (sockaddr*)&addr, sizeof addr);
listen(lfd, 16);
for (;;) {
    int cfd = accept(lfd, nullptr, nullptr);
    // hand cfd off to your event loop / per-connection state
}
```
For two-player Pong with rare connections you don't even need `epoll`/`kqueue` - a single-threaded `poll()` loop over all open fds is fine.

### 2. Do the WebSocket upgrade handshake

When a new connection arrives, read the HTTP request line + headers (terminated by `\r\n\r\n`). Find `Sec-WebSocket-Key`, compute:

```
Sec-WebSocket-Accept = base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
```
(That GUID is literally hard-coded in RFC 6455.) Send back:

```
HTTP/1.1 101 Switching Protocols
Upgrade: websocket
Connection: Upgrade
Sec-WebSocket-Accept: <computed>

```
You'll need a SHA-1 implementation and base64 encoder. Both are ~30 lines each, both are RFC'd. Done.

### 3. Parse and emit WebSocket frames

This is the real work. The frame format is the bit-fiddling diagram from the earlier reply:

```
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |  Extended payload length...   |
+-+-+-+-+-------+-+-------------+-------------------------------+
| Masking key (only if MASK=1)                                  |
+---------------------------------------------------------------+
| Payload data...                                               |
```
Things to handle:
- **FIN bit** - frames can be fragmented; ours never need to be, but you should be lenient on input.
- **Opcode** - 0x1 text, 0x2 binary, 0x8 close, 0x9 ping, 0xA pong. Reply to pings with pongs; honor close.
- **Payload length** - 0–125 inline, 126 = next 2 bytes are length, 127 = next 8 bytes.
- **Mask** - *required* on client→server frames, *forbidden* on server→client. Client masks payload by XOR-ing with a 4-byte key; you unmask on read.

~100 lines of careful C++. This is the part to write tests for.

### 4. Pair peers into sessions

Simplest possible version that's actually useful:

```cpp
struct Session { int host = -1; int guest = -1; };
std::unordered_map<std::string, Session> rooms;  // keyed by room code
```
When a client connects and sends `{"kind":"join","room":"ABCD"}`:
- If room doesn't exist, create it with this fd as `host`.
- If room exists with only a host, set `guest`, send both `{"kind":"ready","role":"host"|"guest"}`.
- If both slots are full, send an error and close.

You only need to JSON-parse the `kind` field of the *envelope*. You can even skip JSON and use a 1-byte tag prefix if you want to keep "no third-party libraries" pure - `loader.js` is the only counter-party so you control both ends of the format.

### 5. Relay messages

When a frame arrives on `host`'s fd with envelope `{"kind":"relay", ...}`, write the same frame payload back out on `guest`'s fd, and vice versa. **You don't parse `payload` at all** - to your server, it's an opaque byte slice. That's the whole hot path.

When either fd closes (or any error), close the other one and delete the room.

## Server protocol design (the shape I'd ship)

Three message kinds, all JSON-over-WebSocket-text:

```js
// client -> server
{ "kind": "join",  "room": "ABCD" }
{ "kind": "relay", "payload": <opaque> }

// server -> client
{ "kind": "ready", "role": "host"|"guest" }
{ "kind": "relay", "payload": <opaque> }   // forwarded from peer
{ "kind": "error", "reason": "<string>" }
{ "kind": "peer_left" }
```
Server only inspects `kind` and (for `join`) `room`. The `payload` blob is whatever your `loader.js` puts in it - typically `{ type: "offer"|"answer"|"ice", ... }`.

## Suggested build order

1. WebSocket framing + HTTP upgrade in C++. Unit tests for masking, length forms, ping/pong, close. **Get this right before anything else; everything depends on it.**
2. Single-room mode (first two connections paired, third rejected). Hard-code the room.
3. Wire up `loader.js` to do the full handshake (createOffer, exchange ICE, etc.) and confirm `dc.onopen` fires across two browser tabs on your laptop.
4. Add multi-room support (room codes from URL).
5. Then start on actual game protocol over the data channel.

The handshake choreography in `loader.js` is where most of the conceptual complexity lives. The server itself, once the WebSocket framing is solid, is genuinely just a TCP relay with a tiny pairing table.




# ok... do i also want this signaling server host some html for "these rooms are available / host a new game"?

Two questions tangled together — worth separating before deciding:

1. **Where does the static HTML/JS/wasm live?** (anywhere — it's just files)
2. **Where does the dynamic lobby data come from?** ("rooms available", "is room X open"… *has* to come from the signaling server, because that's the only thing that knows the current state)

(2) is non-negotiable. (1) is a deployment choice.

## My recommendation: don't host static files from the signaling server

Build the lobby as **another set of WebSocket message kinds** on the signaling server you're already writing, and serve the HTML from anywhere else.

```
client/JS                                signaling server
─────────                                ─────────────────
ws.send({kind:"list_rooms"})       ─→
                                   ←─    {kind:"rooms", rooms:[{code:"ABCD", host:"alice"}, ...]}
ws.send({kind:"create_room"})      ─→
                                   ←─    {kind:"created", code:"WXYZ"}
ws.send({kind:"join", room:"ABCD"})─→
                                   ←─    {kind:"ready", role:"guest"}
```
Then `index.html` just has two views: a lobby view (rooms list + "host" button) and a game view (canvas). On page load, JS opens the WebSocket and asks for the room list — the same socket later carries the SDP/ICE handshake once a room is joined. **One protocol, one connection, one server.**

Why this beats serving HTML from the signaling server:

- **Static hosting is already free.** GitHub Pages, Cloudflare Pages, Netlify, even an S3 bucket — all give you HTTPS + CDN for nothing. Your signaling server doesn't need to be a web server.
- **Writing a static file server isn't interesting code.** It's MIME-type tables, path traversal checks, range requests, caching headers — boring, error-prone, and not what the project is about. The "no third-party libraries" ethos is *much* more interesting applied to WebSocket + signaling logic than to "read file, write bytes, set Content-Type."
- **Smaller attack surface.** A signaling server that only speaks WebSocket has roughly one entry point. A web server has dozens of edge cases (path traversal, symlinks, partial reads, etc.).
- **HTTPS pain stays small.** WebRTC requires HTTPS in production (or localhost). If you host static files on GH Pages, they get HTTPS for free. You only need to put TLS in front of your signaling server's WebSocket — typically a 5-line Caddy config or an nginx reverse proxy.

## The counter-argument (for completeness)

Folding static hosting into the signaling server has one real upside: **a single deploy artifact**. "Run `./signal` on a $5 VPS, point DNS at it, done." For a hobby project that's genuinely nice. And you're already going to write HTTP parsing for the WebSocket upgrade — extending it to also handle `GET /index.html` is maybe 30–50 lines of boring code (parse path, read file, send response with `Content-Type` from a tiny extension→MIME map).

If that simpler deploy story matters more to you than the cleaner separation, fold it in. It's not architecturally wrong; it's just adding scope to the part of the project that's the least fun.

## What I'd actually do for your project

1. **Phase 1 (now → MVP):** signaling server is WebSocket-only. Keep using `python3 -m http.server` from `web/`. Lobby operations are WebSocket message kinds (`list_rooms`, `create_room`, `join`).
2. **Phase 2 (deploy):** push `web/` to GitHub Pages (free, automatic from a branch). Stick Caddy (or any reverse proxy) in front of your signaling server for `wss://` TLS termination.
3. **Phase 3 (optional, if you ever want):** add a static-file handler to the signaling server so it can serve `web/` itself, for single-binary deployments. Easy to bolt on later if you decide you want it; near-impossible to remove cleanly once it's load-bearing.

This way the signaling server stays a focused little networking experiment, not a half-finished web framework.


# "what's up with src/signal/main.cpp?"

This is the classic BSD-sockets "set up a TCP listener" sequence — the same five steps every Unix server has been doing since the early '80s. Walking through it line by line:

## `int fd = socket(AF_INET, SOCK_STREAM, 0);`

Asks the kernel for a brand-new socket and returns a file descriptor (small integer) you can use to refer to it.

- **`AF_INET`** = address family **Internet** = IPv4. (`AF_INET6` would be IPv6.)
- **`SOCK_STREAM`** = a reliable, ordered, byte-stream socket — i.e. TCP. (`SOCK_DGRAM` would be UDP.)
- **`0`** = "let the kernel pick the default protocol for that combo." For `AF_INET` + `SOCK_STREAM`, the default is TCP.

Returns `-1` on failure. **`perror("socket")`** prints the system's error message ("socket: Too many open files", etc.) to stderr; **`exit(1)`** bails out.

## `setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);`

Tweaks an option on the socket. **`SO_REUSEADDR`** says: *"if this address is in `TIME_WAIT`, let me bind to it anyway."*

Without this flag, the very common workflow of "kill server, restart it on the same port" fails for ~30–60 seconds with `bind: Address already in use`. That's because TCP keeps the address reserved after a connection closes, in case stragglers are still in flight. `SO_REUSEADDR` says "I know, don't care, give it to me." Mandatory for any server you'll restart frequently.

`setsockopt` is generic across all socket options, hence the awkward `(&yes, sizeof yes)` — it takes a `void*` + length so the same function can pass any kind of option payload (an `int`, a `struct linger`, etc.).

## `sockaddr_in addr{};`

The struct that describes "where to bind" — IP address + port. The `{}` zero-initializes it, which is important because we only fill in three of its fields and want the rest to be zero (especially `sin_zero`, an 8-byte padding field that's required to be zero).

## Filling in the address

```cpp
addr.sin_family = AF_INET;                 // "this is an IPv4 address"
addr.sin_port = htons(port);               // port number, big-endian
addr.sin_addr.s_addr = htonl(INADDR_ANY);  // bind on all interfaces
```
- **`sin_family = AF_INET`** — has to match the socket's family. The kernel checks.
- **`sin_port = htons(port)`** — port in network byte order, as covered.
- **`sin_addr.s_addr = htonl(INADDR_ANY)`** — `INADDR_ANY` is the constant `0.0.0.0`, which means "accept connections on every network interface this machine has" — `127.0.0.1` (localhost), the LAN IP, the public IP, etc. The alternative `INADDR_LOOPBACK` (`127.0.0.1`) would only accept local connections. For a server you want `ANY`. `htonl` is technically a no-op for the value `0`, but it's the convention to call it anyway so the line is correct for any future address.

## `bind(fd, (sockaddr*)&addr, sizeof addr)`

Tells the kernel: "associate this socket with this IP+port." After `bind`, the socket "owns" `0.0.0.0:port` until it closes.

The cast `(sockaddr*)&addr` exists because `bind` takes the generic `sockaddr*`, and the kernel reads the family field to figure out which concrete struct (`sockaddr_in` for IPv4, `sockaddr_in6` for IPv6, `sockaddr_un` for Unix sockets) was actually passed. C's way of doing polymorphism in 1983.

Returns -1 on failure. The most common failures are `EADDRINUSE` (port taken) and `EACCES` (trying to bind to a port < 1024 without root).

## `listen(fd, 16)`

Marks the socket as a **passive listener**: it won't initiate connections, only accept incoming ones. Required before you can call `accept()` on it.

The `16` is the **backlog** — how many half-completed handshakes the kernel will queue up while you're busy and haven't called `accept()` yet. If 17 clients try to connect simultaneously and you haven't accepted any, the 17th gets a connection-refused. For a hobby Pong signaling server with 2 expected concurrent connections, 16 is wildly generous; the number doesn't really matter.

## `return fd;`

Hand the now-listening socket back to the caller. From here, you call `accept(fd)` in a loop to pull off new client connections.

## The big picture

The full TCP-server lifecycle is:

```
                   ┌──────────┐
        socket() ─►│  fd      │  newly created, unconfigured
                   └────┬─────┘
                        │  setsockopt (optional tweaks)
                        │  bind        (claim IP:port)
                        ▼
                   ┌──────────┐
        listen() ─►│ LISTENING│  ready to accept connections
                   └────┬─────┘
                        │  accept() returns a NEW fd per client
                        ▼
                   ┌──────────┐  ┌──────────┐  ┌──────────┐
                   │ client A │  │ client B │  │ client C │ ...
                   └──────────┘  └──────────┘  └──────────┘
                       read/write on these to talk to each client
```
`make_listener` does steps 1–3. The `poll()` loop in `main()` does step 4 (accept) and step 5 (read/write). The listener fd stays alive forever; per-client fds come and go.