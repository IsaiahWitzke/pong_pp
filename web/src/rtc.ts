// Rtc wraps a single RTCPeerConnection + RTCDataChannel for one match.
// All SDP/ICE traffic is shipped through the Signaling layer's RELAY
// pipe; the inbound side is wired in via Signaling.onRelay. Today the
// signaling server's RELAY only ever carries SDP/ICE so we don't bother
// with a tag prefix — if that changes, add one then.

import type { Signaling } from "./signaling.js";
import { Role } from "./types.js";

interface SignalEnvelope {
    t: "offer" | "answer" | "ice";
    sdp?: string;
    c?: RTCIceCandidateInit;
}

const RTC_CONFIG: RTCConfiguration = {
    iceServers: [{ urls: "stun:stun.l.google.com:19302" }],
};

export class Rtc {
    onOpen: () => void = () => {};
    onClose: () => void = () => {};
    onMessage: (text: string) => void = () => {};
    onConnectionStateChange: (state: RTCPeerConnectionState) => void = () => {};

    private pc: RTCPeerConnection | null = null;
    private dc: RTCDataChannel | null = null;

    constructor(private readonly signaling: Signaling) {
        // Every RELAY today is a SignalEnvelope; consume them all here.
        signaling.onRelay = (body) => this.handleSignal(body);
    }

    async init(role: Role): Promise<void> {
        this.close(); // tear down any leftover from a previous match

        const pc = new RTCPeerConnection(RTC_CONFIG);
        this.pc = pc;

        // Trickle ICE: emit each candidate as soon as it's discovered.
        pc.onicecandidate = (e) => {
            if (e.candidate) this.sendSignal({ t: "ice", c: e.candidate.toJSON() });
        };
        pc.onconnectionstatechange = () => {
            if (this.pc) this.onConnectionStateChange(this.pc.connectionState);
        };

        if (role === Role.Host) {
            // Offerer creates the channel up front so it appears in the SDP.
            this.wireDataChannel(pc.createDataChannel("game", { ordered: true }));
            const offer = await pc.createOffer();
            await pc.setLocalDescription(offer);
            this.sendSignal({ t: "offer", sdp: offer.sdp });
        } else {
            // Answerer waits for the host's offer to deliver the channel.
            pc.ondatachannel = (e) => this.wireDataChannel(e.channel);
        }
    }

    send(text: string): void {
        if (this.dc?.readyState === "open") this.dc.send(text);
    }

    close(): void {
        try {
            this.dc?.close();
        } catch {
            /* swallow */
        }
        try {
            this.pc?.close();
        } catch {
            /* swallow */
        }
        this.dc = null;
        this.pc = null;
    }

    // ── internals ────────────────────────────────────────────────────

    private wireDataChannel(channel: RTCDataChannel): void {
        this.dc = channel;
        channel.binaryType = "arraybuffer";
        channel.onopen = () => this.onOpen();
        channel.onclose = () => this.onClose();
        channel.onmessage = (e) => {
            const text =
                typeof e.data === "string" ? e.data : new TextDecoder().decode(new Uint8Array(e.data as ArrayBuffer));
            this.onMessage(text);
        };
    }

    private sendSignal(env: SignalEnvelope): void {
        this.signaling.relay(JSON.stringify(env));
    }

    private async handleSignal(json: string): Promise<void> {
        if (!this.pc) return;
        let m: SignalEnvelope;
        try {
            m = JSON.parse(json) as SignalEnvelope;
        } catch {
            return;
        }

        if (m.t === "offer" && m.sdp !== undefined) {
            await this.pc.setRemoteDescription({ type: "offer", sdp: m.sdp });
            const answer = await this.pc.createAnswer();
            await this.pc.setLocalDescription(answer);
            this.sendSignal({ t: "answer", sdp: answer.sdp });
        } else if (m.t === "answer" && m.sdp !== undefined) {
            await this.pc.setRemoteDescription({ type: "answer", sdp: m.sdp });
        } else if (m.t === "ice" && m.c !== undefined) {
            try {
                await this.pc.addIceCandidate(m.c);
            } catch {
                /* ignore late/dup */
            }
        }
    }
}
