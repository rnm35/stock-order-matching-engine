// A simulated market around one engine: bots that generate order flow, a user (the person
// in the browser, or Claude through the MCP server) placing orders, and the derived views
// the UI shows (trade tape, the user's orders, depth). Shared by the web app and MCP server.

import { Engine, loadEngine } from "../engine/engine.ts";
import type { BookEvent, Level, Mode, Side, TimeInForce } from "../engine/engine.ts";
import { createBots } from "./bots.ts";
import type { Bot } from "./bots.ts";
import { Random } from "./random.ts";

export interface TradePrint {
    seq: number;
    price: number;
    quantity: number;
    aggressor: Side;
    buyId: number;
    sellId: number;
    /** Set when one side of the trade is a user order. */
    userSide: Side | null;
}

export type UserOrderStatus = "open" | "filled" | "cancelled" | "rejected";

export interface UserOrder {
    id: number;
    side: Side;
    orderType: "limit" | "market";
    price: number | null;
    tif: TimeInForce;
    quantity: number;
    filled: number;
    remaining: number;
    status: UserOrderStatus;
    note: string;
}

export interface Snapshot {
    bids: Level[];
    asks: Level[];
    bestBid: number | null;
    bestAsk: number | null;
    lastPrice: number | null;
    restingOrders: number;
    tape: TradePrint[];
    userOrders: UserOrder[];
    fairValue: number;
    stats: MarketStats;
}

export interface MarketStats {
    requests: number;
    trades: number;
    volume: number;
}

export interface MarketOptions {
    seed?: number;
    mode?: Mode;
    /** Starting fair value and (legacy mode) initial last traded price, in ticks. */
    startPrice?: number;
}

const TAPE_LENGTH = 200;

/** Placed by one of the bots, the user, or nobody (unknown id). */
type Owner = { kind: "bot"; bot: Bot } | { kind: "user" };

export class Market {
    readonly engine: Engine;
    readonly rng: Random;
    readonly bots: Bot[];
    fairValue: number;
    readonly stats: MarketStats = { requests: 0, trades: 0, volume: 0 };

    private nextId = 1;
    private tradeSeq = 0;
    private readonly owners = new Map<number, Owner>();
    private readonly tape: TradePrint[] = [];
    private readonly userOrders = new Map<number, UserOrder>();

    private constructor(engine: Engine, options: Required<MarketOptions>) {
        this.engine = engine;
        this.rng = new Random(options.seed);
        this.fairValue = options.startPrice;
        this.bots = createBots(this);
    }

    static async create(options: MarketOptions = {}): Promise<Market> {
        const full: Required<MarketOptions> = {
            seed: options.seed ?? 1,
            mode: options.mode ?? "modern",
            startPrice: options.startPrice ?? 10_000,
        };
        const engine = new Engine(await loadEngine(), full.mode, full.startPrice);
        return new Market(engine, full);
    }

    dispose(): void {
        this.engine.dispose();
    }

    /** Runs `count` bot actions (each bot action is one or a few requests). */
    step(count: number): void {
        for (let i = 0; i < count; ++i) {
            // Fair value drifts as a slow random walk; the bots quote and trade around it.
            if (this.rng.chance(0.05)) this.fairValue = Math.max(100, this.fairValue + this.rng.int(-1, 1));
            const bot = this.pickBot();
            if (bot) bot.act(this.rng);
        }
    }

    // --- Requests (all flow, bot or user, goes through these) ---------------------------

    submitLimit(owner: Owner, side: Side, quantity: number, price: number, tif: TimeInForce = "GTC"): number {
        const id = this.nextId++;
        this.owners.set(id, owner);
        this.record(() => this.engine.submitLimit(id, side, quantity, price, tif));
        return id;
    }

    submitMarket(owner: Owner, side: Side, quantity: number, tif: TimeInForce = "GTC"): number {
        const id = this.nextId++;
        this.owners.set(id, owner);
        this.record(() => this.engine.submitMarket(id, side, quantity, tif));
        return id;
    }

    cancel(id: number): void {
        this.record(() => this.engine.cancel(id));
    }

    modify(id: number, price: number, quantity: number): void {
        this.record(() => this.engine.modify(id, price, quantity));
    }

    // --- The user ----------------------------------------------------------------------

    placeUserOrder(order: {
        side: Side;
        orderType: "limit" | "market";
        quantity: number;
        price?: number;
        tif?: TimeInForce;
    }): UserOrder {
        const tif = order.tif ?? "GTC";
        const user: Owner = { kind: "user" };
        if (order.orderType === "limit" && order.price === undefined) {
            throw new Error("a limit order needs a price");
        }
        const id = this.nextId; // assigned by submit*() below
        this.userOrders.set(id, {
            id,
            side: order.side,
            orderType: order.orderType,
            price: order.orderType === "limit" ? (order.price ?? null) : null,
            tif,
            quantity: order.quantity,
            filled: 0,
            remaining: order.quantity,
            status: "open",
            note: "",
        });
        if (order.orderType === "limit") {
            this.submitLimit(user, order.side, order.quantity, order.price as number, tif);
        } else {
            this.submitMarket(user, order.side, order.quantity, tif);
        }
        return { ...(this.userOrders.get(id) as UserOrder) };
    }

    cancelUserOrder(id: number): UserOrder | null {
        const order = this.userOrders.get(id);
        if (!order) return null;
        if (order.status === "open") this.cancel(id);
        return { ...order };
    }

    userOrder(id: number): UserOrder | null {
        const order = this.userOrders.get(id);
        return order ? { ...order } : null;
    }

    // --- Views ---------------------------------------------------------------------------

    snapshot(levels = 20, tapeLength = 50): Snapshot {
        return {
            bids: this.engine.depth("buy", levels),
            asks: this.engine.depth("sell", levels),
            bestBid: this.engine.bestBid(),
            bestAsk: this.engine.bestAsk(),
            lastPrice: this.engine.lastTradePrice(),
            restingOrders: this.engine.orderCount(),
            tape: this.tape.slice(-tapeLength).reverse(),
            userOrders: [...this.userOrders.values()].reverse().map((o) => ({ ...o })),
            fairValue: this.fairValue,
            stats: { ...this.stats },
        };
    }

    // --- Internals -------------------------------------------------------------------------

    private pickBot(): Bot | undefined {
        const active = this.bots.filter((b) => b.enabled);
        if (active.length === 0) return undefined;
        let total = 0;
        for (const b of active) total += b.weight;
        let roll = this.rng.next() * total;
        for (const b of active) {
            roll -= b.weight;
            if (roll < 0) return b;
        }
        return active[active.length - 1];
    }

    private record(call: () => BookEvent[]): void {
        const events = call();
        this.stats.requests += 1;
        for (const e of events) this.apply(e);
        // Forget owners of orders that are done (filled). Only now, after the whole request:
        // an incoming order isn't resting while it is still matching, so checking per trade
        // would drop it after its first fill and lose the rest.
        for (const e of events) {
            if (e.type !== "trade") continue;
            for (const id of [e.buyId, e.sellId]) {
                if (this.owners.has(id) && !this.engine.find(id)) this.owners.delete(id);
            }
        }
    }

    private apply(e: BookEvent): void {
        switch (e.type) {
            case "trade": {
                this.stats.trades += 1;
                this.stats.volume += e.quantity;
                const buyer = this.owners.get(e.buyId);
                const seller = this.owners.get(e.sellId);
                let userSide: Side | null = null;
                if (buyer?.kind === "user") userSide = "buy";
                if (seller?.kind === "user") userSide = "sell";
                this.tape.push({
                    seq: ++this.tradeSeq,
                    price: e.price,
                    quantity: e.quantity,
                    aggressor: e.aggressor,
                    buyId: e.buyId,
                    sellId: e.sellId,
                    userSide,
                });
                if (this.tape.length > TAPE_LENGTH * 2) this.tape.splice(0, this.tape.length - TAPE_LENGTH);
                this.fill(e.buyId, buyer, "buy", e.quantity, e.price);
                this.fill(e.sellId, seller, "sell", e.quantity, e.price);
                break;
            }
            case "cancelled": {
                const order = this.userOrders.get(e.id);
                if (order) {
                    order.status = "cancelled";
                    order.remaining = 0;
                    order.note = e.reason;
                }
                this.owners.delete(e.id);
                break;
            }
            case "rejected": {
                const order = this.userOrders.get(e.id);
                if (order && e.request === "new") {
                    order.status = "rejected";
                    order.remaining = 0;
                    order.note = e.reason;
                }
                break;
            }
            case "modified": {
                const order = this.userOrders.get(e.id);
                if (order) {
                    order.price = e.price;
                    order.remaining = e.quantity;
                }
                break;
            }
            case "accepted":
                break;
        }
    }

    private fill(id: number, owner: Owner | undefined, side: Side, quantity: number, price: number): void {
        if (owner?.kind === "bot") {
            owner.bot.onFill(id, side, quantity, price);
        } else if (owner?.kind === "user") {
            const order = this.userOrders.get(id);
            if (order) {
                order.filled += quantity;
                order.remaining -= quantity;
                if (order.remaining === 0) order.status = "filled";
            }
        }
    }
}

export const USER: Owner = { kind: "user" };
export type { Owner };
