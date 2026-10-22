// Simulated traders that give the book realistic-looking flow. Each `act()` sends one or a
// few requests through the Market (so they are recorded exactly like user orders).

import type { Side } from "../engine/engine.ts";
import type { Market, Owner } from "./market.ts";
import type { Random } from "./random.ts";

export interface Bot {
    readonly name: string;
    enabled: boolean;
    /** Relative share of actions this bot gets. */
    weight: number;
    act(rng: Random): void;
    onFill(id: number, side: Side, quantity: number, price: number): void;
}

interface MarketMakerConfig {
    levels: number;      // quotes per side
    halfSpread: number;  // ticks from fair value to the innermost quote
    spacing: number;     // ticks between quote levels
    size: number;        // shares per quote
    skewPerTick: number; // inventory (shares) that shifts the quotes by one tick
    weight: number;
}

/**
 * Keeps `levels` quotes on each side around fair value. Each action re-checks one quote:
 * places it if missing (filled or never placed), or moves it with modify() if fair value or
 * the bot's inventory has moved. Inventory skew: a long market maker lowers its prices to
 * sell more and buy less, which keeps it from accumulating a large position.
 */
export class MarketMaker implements Bot {
    readonly name: string;
    enabled = true;
    weight: number;
    inventory = 0;

    private readonly market: Market;
    private readonly config: MarketMakerConfig;
    private readonly owner: Owner;
    private readonly quotes = new Map<string, number>(); // "buy:0" -> order id

    constructor(market: Market, name: string, config: MarketMakerConfig) {
        this.market = market;
        this.name = name;
        this.config = config;
        this.weight = config.weight;
        this.owner = { kind: "bot", bot: this };
    }

    targetPrice(side: Side, level: number): number {
        const { halfSpread, spacing, skewPerTick } = this.config;
        const skew = -Math.round(this.inventory / skewPerTick);
        const offset = halfSpread + level * spacing;
        const price = this.market.fairValue + skew + (side === "buy" ? -offset : offset);
        return Math.max(1, price);
    }

    act(rng: Random): void {
        const side: Side = rng.chance(0.5) ? "buy" : "sell";
        const level = rng.int(0, this.config.levels - 1);
        const key = `${side}:${level}`;
        const target = this.targetPrice(side, level);
        const id = this.quotes.get(key);
        const live = id === undefined ? null : this.market.engine.find(id);

        if (id === undefined || live === null) {
            this.quotes.set(key, this.market.submitLimit(this.owner, side, this.config.size, target));
        } else if (live.price !== target || live.remaining < this.config.size / 4) {
            // Re-price (or top up a mostly filled quote); both lose queue priority.
            this.market.modify(id, target, this.config.size);
        }
    }

    onFill(_id: number, side: Side, quantity: number): void {
        this.inventory += side === "buy" ? quantity : -quantity;
    }
}

/**
 * Uninformed flow: limit orders scattered around fair value (about a quarter priced through
 * the spread, so they trade), market orders, some IOC, and cancels of its own old orders.
 */
export class NoiseTraders implements Bot {
    readonly name: string;
    enabled = true;
    weight: number;

    private readonly market: Market;
    private readonly owner: Owner;
    private readonly open: number[] = [];

    constructor(market: Market, name: string, weight: number) {
        this.market = market;
        this.name = name;
        this.weight = weight;
        this.owner = { kind: "bot", bot: this };
    }

    act(rng: Random): void {
        const side: Side = rng.chance(0.5) ? "buy" : "sell";
        const roll = rng.next();
        if (roll < 0.25 && this.open.length > 0) {
            const i = rng.int(0, this.open.length - 1);
            const id = this.open[i] as number;
            this.open.splice(i, 1);
            if (this.market.engine.find(id)) this.market.cancel(id);
        } else if (roll < 0.4) {
            this.market.submitMarket(this.owner, side, rng.int(1, 300));
        } else {
            // Positive offset = passive (behind fair value), negative = crosses it.
            const offset = rng.int(-2, 10);
            const price = Math.max(1, this.market.fairValue + (side === "buy" ? -offset : offset));
            const tif = rng.chance(0.1) ? "IOC" : "GTC";
            const id = this.market.submitLimit(this.owner, side, rng.int(1, 200), price, tif);
            if (tif === "GTC") this.open.push(id);
            if (this.open.length > 60) {
                const oldest = this.open.shift() as number;
                if (this.market.engine.find(oldest)) this.market.cancel(oldest);
            }
        }
    }

    onFill(): void {}
}

export function createBots(market: Market): Bot[] {
    return [
        new MarketMaker(market, "Market maker (tight)", {
            levels: 3, halfSpread: 1, spacing: 1, size: 200, skewPerTick: 400, weight: 3,
        }),
        new MarketMaker(market, "Market maker (deep)", {
            levels: 8, halfSpread: 3, spacing: 2, size: 600, skewPerTick: 1500, weight: 3,
        }),
        new NoiseTraders(market, "Noise traders", 4),
    ];
}
