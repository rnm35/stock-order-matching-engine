import { afterEach, describe, expect, it } from "vitest";

import { Market } from "./market.ts";

const markets: Market[] = [];
async function market(seed = 7) {
    const m = await Market.create({ seed });
    markets.push(m);
    return m;
}
afterEach(() => {
    for (const m of markets.splice(0)) m.dispose();
});

describe("Market simulation", () => {
    it("is reproducible for a given seed", async () => {
        const a = await market(42);
        const b = await market(42);
        a.step(2_000);
        b.step(2_000);
        expect(a.snapshot().tape).toEqual(b.snapshot().tape);
        expect(a.snapshot().bids).toEqual(b.snapshot().bids);
    });

    it("bots keep a two-sided, uncrossed book with trading", async () => {
        const m = await market();
        for (let i = 0; i < 20; ++i) {
            m.step(500);
            const s = m.snapshot();
            expect(s.bestBid).not.toBeNull();
            expect(s.bestAsk).not.toBeNull();
            expect(s.bestBid as number).toBeLessThan(s.bestAsk as number);
        }
        expect(m.stats.trades).toBeGreaterThan(100);
    });

    it("tracks a user limit order from rest to cancel", async () => {
        const m = await market();
        m.step(1_000);
        const farBelow = (m.snapshot().bestBid as number) - 50;
        const placed = m.placeUserOrder({ side: "buy", orderType: "limit", quantity: 10, price: farBelow });
        expect(placed).toMatchObject({ status: "open", remaining: 10, filled: 0 });
        expect(m.engine.find(placed.id)).not.toBeNull();

        expect(m.cancelUserOrder(placed.id)).toMatchObject({ status: "cancelled", remaining: 0 });
        expect(m.engine.find(placed.id)).toBeNull();
    });

    it("fills a user market order against the market makers and marks it on the tape", async () => {
        const m = await market();
        m.step(1_000);
        const order = m.placeUserOrder({ side: "buy", orderType: "market", quantity: 50 });
        expect(order).toMatchObject({ status: "filled", filled: 50, remaining: 0 });
        const mine = m.snapshot().tape.filter((t) => t.userSide === "buy");
        expect(mine.reduce((sum, t) => sum + t.quantity, 0)).toBe(50);
    });

    it("counts every fill of a user order that sweeps several resting orders", async () => {
        const m = await market();
        m.step(1_000);
        // Larger than any single bot quote (max 600), so it must trade with several orders.
        const quantity = 1_500;
        const order = m.placeUserOrder({ side: "buy", orderType: "market", quantity });
        const fills = m.snapshot(1, 500).tape.filter((t) => t.buyId === order.id);
        expect(fills.length).toBeGreaterThan(1);
        const traded = fills.reduce((sum, t) => sum + t.quantity, 0);
        expect(order.filled).toBe(traded);
        expect(order.filled + order.remaining).toBe(quantity);
        expect(order.status).toBe(traded === quantity ? "filled" : "cancelled");
    });

    it("records engine rejects on the user's order", async () => {
        const m = await market();
        const order = m.placeUserOrder({ side: "sell", orderType: "limit", quantity: 0, price: 10_000 });
        expect(order).toMatchObject({ status: "rejected", note: "invalid quantity" });
    });
});
