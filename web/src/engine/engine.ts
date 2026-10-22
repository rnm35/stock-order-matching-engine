// Typed access to the C++ engine compiled to WebAssembly (see wasm/bindings.cpp).
// Used by the browser UI and by the MCP server (under Node), so this file only uses
// TypeScript syntax that Node can strip (no enums, no parameter properties).
//
// The raw module speaks numbers only (it is much cheaper to cross the JS/wasm boundary
// that way); this wrapper turns them into the typed objects below.

import createOme from "./wasm/ome.js";
import type { OmeModule, RawBook } from "./wasm/ome.js";

export type Side = "buy" | "sell";
export type OrderType = "limit" | "market";
export type TimeInForce = "GTC" | "IOC" | "FOK";
export type Mode = "modern" | "legacy";

export type BookEvent =
    | { type: "accepted"; id: number; side: Side; orderType: OrderType; price: number; quantity: number }
    | { type: "trade"; buyId: number; sellId: number; aggressor: Side; price: number; quantity: number }
    | { type: "cancelled"; id: number; quantity: number; reason: string }
    | { type: "modified"; id: number; price: number; quantity: number; priorityKept: boolean }
    | { type: "rejected"; id: number; request: string; reason: string };

export interface Level {
    price: number;
    quantity: number;
    orders: number;
}

export interface RestingOrder {
    id: number;
    side: Side;
    orderType: OrderType;
    price: number;
    remaining: number;
}

// Must match the C++ enums' order and ome::toString() (src/to_string.cpp).
const SIDES: readonly Side[] = ["buy", "sell"];
const ORDER_TYPES: readonly OrderType[] = ["limit", "market"];
const TIF_CODES: Record<TimeInForce, number> = { GTC: 0, IOC: 1, FOK: 2 };
const CANCEL_REASONS = ["user request", "unfilled immediate remainder", "fill-or-kill not fillable"];
const REQUESTS = ["new", "cancel", "modify"];
const REJECT_REASONS = [
    "duplicate order id",
    "invalid quantity",
    "invalid price",
    "unknown order",
    "market orders cannot be modified",
];
const EVENT_WIDTH = 8;

let modulePromise: Promise<OmeModule> | undefined;

/** Loads and instantiates the WebAssembly module once per process/page. */
export function loadEngine(): Promise<OmeModule> {
    modulePromise ??= createOme();
    return modulePromise;
}

function sideCode(side: Side): number {
    if (side !== "buy" && side !== "sell") throw new Error("side must be 'buy' or 'sell'");
    return side === "buy" ? 0 : 1;
}

function tifCode(tif: TimeInForce): number {
    const code = TIF_CODES[tif];
    if (code === undefined) throw new Error("time in force must be 'GTC', 'IOC' or 'FOK'");
    return code;
}

function decodeEvents(r: Float64Array): BookEvent[] {
    const events: BookEvent[] = [];
    for (let i = 0; i < r.length; i += EVENT_WIDTH) {
        const f = (k: number) => r[i + k] as number;
        switch (f(0)) {
            case 0:
                events.push({
                    type: "accepted",
                    id: f(1),
                    side: SIDES[f(2)] as Side,
                    orderType: ORDER_TYPES[f(3)] as OrderType,
                    price: f(4),
                    quantity: f(5),
                });
                break;
            case 1:
                events.push({
                    type: "trade",
                    buyId: f(1),
                    sellId: f(2),
                    aggressor: SIDES[f(3)] as Side,
                    price: f(4),
                    quantity: f(5),
                });
                break;
            case 2:
                events.push({ type: "cancelled", id: f(1), quantity: f(2), reason: CANCEL_REASONS[f(3)] ?? "?" });
                break;
            case 3:
                events.push({ type: "modified", id: f(1), price: f(2), quantity: f(3), priorityKept: f(4) === 1 });
                break;
            case 4:
                events.push({
                    type: "rejected",
                    id: f(1),
                    request: REQUESTS[f(2)] ?? "?",
                    reason: REJECT_REASONS[f(3)] ?? "?",
                });
                break;
        }
    }
    return events;
}

const orNull = (x: number) => (Number.isNaN(x) ? null : x);

/**
 * One order book. Each call returns the events it caused, in order. Invalid arguments
 * (e.g. a fractional price) throw a JS Error carrying the C++ exception's message.
 */
export class Engine {
    readonly mode: Mode;
    private readonly ome: OmeModule;
    private readonly book: RawBook;

    constructor(ome: OmeModule, mode: Mode, lastTradePrice: number) {
        this.ome = ome;
        this.mode = mode;
        this.book = this.call(() => new ome.Book(mode, lastTradePrice));
    }

    submitLimit(id: number, side: Side, quantity: number, price: number, tif: TimeInForce = "GTC"): BookEvent[] {
        const s = sideCode(side);
        const t = tifCode(tif);
        this.call(() => this.book.submitLimit(id, s, quantity, price, t));
        return decodeEvents(this.book.events());
    }

    submitMarket(id: number, side: Side, quantity: number, tif: TimeInForce = "GTC"): BookEvent[] {
        const s = sideCode(side);
        const t = tifCode(tif);
        this.call(() => this.book.submitMarket(id, s, quantity, t));
        return decodeEvents(this.book.events());
    }

    cancel(id: number): BookEvent[] {
        this.call(() => this.book.cancel(id));
        return decodeEvents(this.book.events());
    }

    modify(id: number, price: number, quantity: number): BookEvent[] {
        this.call(() => this.book.modify(id, price, quantity));
        return decodeEvents(this.book.events());
    }

    bestBid(): number | null {
        return orNull(this.book.bestBid());
    }
    bestAsk(): number | null {
        return orNull(this.book.bestAsk());
    }
    lastTradePrice(): number | null {
        return orNull(this.book.lastTradePrice());
    }
    orderCount(): number {
        return this.book.orderCount();
    }

    depth(side: Side, maxLevels: number): Level[] {
        const r = this.book.depth(sideCode(side), maxLevels);
        const levels: Level[] = [];
        for (let i = 0; i < r.length; i += 3) {
            levels.push({ price: r[i] as number, quantity: r[i + 1] as number, orders: r[i + 2] as number });
        }
        return levels;
    }

    find(id: number): RestingOrder | null {
        const r = this.call(() => this.book.find(id));
        if (r.length === 0) return null;
        return {
            id,
            side: SIDES[r[0] as number] as Side,
            orderType: ORDER_TYPES[r[1] as number] as OrderType,
            price: r[2] as number,
            remaining: r[3] as number,
        };
    }

    /** Frees the C++ book. */
    dispose(): void {
        this.book.delete();
    }

    // C++ exceptions arrive as opaque WebAssembly exceptions; turn them into Errors.
    private call<T>(fn: () => T): T {
        try {
            return fn();
        } catch (e) {
            if (e instanceof Error) throw e;
            const [, message] = this.ome.getExceptionMessage(e);
            this.ome.decrementExceptionRefcount(e);
            throw new Error(message);
        }
    }
}
