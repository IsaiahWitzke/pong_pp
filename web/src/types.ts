// Shared types. Const-object enums keep the underlying value an `i32`
// (so it crosses the WASM ABI cheaply) while giving every TS callsite a
// real named type.

export const Role = { Host: 0, Guest: 1 } as const;
export type  Role = typeof Role[keyof typeof Role];

export const Screen = { Menu: 0, Waiting: 1, Game: 2 } as const;
export type  Screen = typeof Screen[keyof typeof Screen];

export interface WasmExports {
    readonly memory: WebAssembly.Memory;

    init(): void;
    tick(): void;

    get_msg_buf(): number;
    get_msg_buf_size(): number;

    start_game(role: Role): void;
    stop_game(): void;
    on_peer_message(len: number): void;
}
