// Signaling owns the WebSocket connection to the lobby/relay server and
// translates the line-oriented verb protocol into a typed event stream.
//
// Wire protocol (matches src/server/signaling/player.cpp):
//   client -> server : LIST | CREATE | JOIN <code> | RELAY <body>
//   server -> client : ROOMS [code...] | CREATED <code> | READY <host|guest>
//                      RELAY <body> | PEER_LEFT | ERROR <reason>

import { Role } from "./types.js";

export type Handler<T> = (arg: T) => void;

export class Signaling {
    // Event hooks. Assigned by the consumer; default no-ops keep call sites
    // Today we assume all RELAY messages contain a WebRTC signaling envelope (Rtc owns onRelay)
    onOpen: () => void = () => {};
    onClose: () => void = () => {};
    onWsError: () => void = () => {};
    onRooms: Handler<readonly string[]> = () => {};
    onCreated: Handler<string> = () => {};
    onReady: Handler<Role> = () => {};
    onPeerLeft: () => void = () => {};
    onError: Handler<string> = () => {};
    onRelay: Handler<string> = () => {};

    private ws: WebSocket | null = null;

    constructor(private readonly url: string) {}

    connect(): void {
        const ws = new WebSocket(this.url);
        this.ws = ws;
        ws.addEventListener("open", () => this.onOpen());
        ws.addEventListener("close", () => {
            this.ws = null;
            this.onClose();
        });
        ws.addEventListener("error", () => this.onWsError());
        ws.addEventListener("message", (e) => this.handleMessage(e.data));
    }

    close(): void {
        this.ws?.close();
    }

    // ── Outbound verbs ────────────────────────────────────────────────
    list(): void {
        this.send("LIST");
    }
    create(): void {
        this.send("CREATE");
    }
    join(code: string): void {
        this.send(`JOIN ${code}`);
    }
    relay(body: string): void {
        this.send(`RELAY ${body}`);
    }

    private send(line: string): void {
        if (this.ws?.readyState === WebSocket.OPEN) this.ws.send(line);
    }

    // ── Inbound dispatch ──────────────────────────────────────────────
    private handleMessage(data: unknown): void {
        if (typeof data !== "string") return; // server only ever sends text
        const sp = data.indexOf(" ");
        const verb = sp === -1 ? data : data.slice(0, sp);
        const body = sp === -1 ? "" : data.slice(sp + 1);

        switch (verb) {
            case "ROOMS":
                this.onRooms(body ? body.split(" ") : []);
                return;
            case "CREATED":
                this.onCreated(body);
                return;
            case "READY":
                this.onReady(body === "host" ? Role.Host : Role.Guest);
                return;
            case "PEER_LEFT":
                this.onPeerLeft();
                return;
            case "ERROR":
                this.onError(body);
                return;
            case "RELAY":
                this.onRelay(body);
                return;
            default:
                /* ignore unknown verbs to stay forward-compatible */ return;
        }
    }
}
