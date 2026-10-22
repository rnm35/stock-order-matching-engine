// The C++ engine through the WebAssembly boundary: the same rules as the C++ tests, checked
// once more here to make sure the bindings convert sides, prices and events correctly.

import { beforeAll, describe, expect, it } from "vitest";

import { measureEngine } from "./benchmark.ts";
import { Engine, loadEngine } from "./engine.ts";
import type { BookEvent } from "./engine.ts";
import { formatPrice, parsePrice, parseQuantity } from "./price.ts";

let ome: Awaited<ReturnType<typeof loadEngine>>;
beforeAll(async () => {
    ome = await loadEngine();
});

const trades = (events: BookEvent[]) => events.filter((e) => e.type === "trade");

describe("Engine (wasm)", () => {
    it("matches at the resting price and reports events in order", () => {
        const book = new Engine(ome, "modern", 0);
        book.submitLimit(1, "sell", 10, 10_000);
        const events = book.submitLimit(2, "buy", 4, 10_050);
        expect(events.map((e) => e.type)).toEqual(["accepted", "trade"]);
        expect(trades(events)[0]).toMatchObject({ buyId: 2, sellId: 1, aggressor: "buy", price: 10_000, quantity: 4 });
        expect(book.depth("sell", 5)).toEqual([{ price: 10_000, quantity: 6, orders: 1 }]);
        expect(book.find(1)).toMatchObject({ side: "sell", price: 10_000, remaining: 6 });
        book.dispose();
    });

    it("keeps FIFO by arrival at one price", () => {
        const book = new Engine(ome, "modern", 0);
        book.submitLimit(10, "sell", 5, 10_000);
        book.submitLimit(2, "sell", 5, 10_000);
        const [trade] = trades(book.submitMarket(3, "buy", 5));
        expect(trade).toMatchObject({ sellId: 10 });
        book.dispose();
    });

    it("cancels a market remainder in modern mode and rests it in legacy mode", () => {
        const modern = new Engine(ome, "modern", 0);
        expect(modern.submitMarket(1, "buy", 5).at(-1)).toMatchObject({ type: "cancelled", quantity: 5 });
        modern.dispose();

        const legacy = new Engine(ome, "legacy", 10_050);
        legacy.submitMarket(1, "buy", 5);
        expect(legacy.find(1)).toMatchObject({ orderType: "market", remaining: 5 });
        legacy.dispose();
    });

    it("modify keeps priority only for a size reduction", () => {
        const book = new Engine(ome, "modern", 0);
        book.submitLimit(1, "buy", 10, 9_900);
        book.submitLimit(2, "buy", 10, 9_900);
        expect(book.modify(1, 9_900, 5)[0]).toMatchObject({ type: "modified", priorityKept: true });
        expect(book.modify(2, 9_900, 20)[0]).toMatchObject({ type: "modified", priorityKept: false });
        book.dispose();
    });

    it("reports rejects as events and invalid arguments as Errors", () => {
        const book = new Engine(ome, "modern", 0);
        expect(book.cancel(42)[0]).toMatchObject({ type: "rejected", request: "cancel", reason: "unknown order" });
        expect(() => book.submitLimit(1, "buy", 1.5, 10_000)).toThrow(/whole number/);
        // @ts-expect-error -- deliberately invalid side
        expect(() => book.submitLimit(1, "sideways", 1, 10_000)).toThrow(/side must be/);
        // The book is still usable after an exception crossed the boundary.
        expect(book.submitLimit(1, "buy", 1, 10_000)[0]).toMatchObject({ type: "accepted" });
        book.dispose();
    });

    it("rejects legacy mode without a last traded price", () => {
        expect(() => new Engine(ome, "legacy", 0)).toThrow(/last traded price/);
    });
});

describe("measureEngine", () => {
    it("times a batch of add/cancel requests", () => {
        const speed = measureEngine(ome, 3_000);
        expect(speed.requests).toBe(4_000); // 3,000 adds + a cancel for every third
        expect(speed.microsPerRequest).toBeGreaterThan(0);
    });
});

describe("price helpers", () => {
    it("formats and parses ticks exactly", () => {
        expect(formatPrice(10_050)).toBe("100.50");
        expect(formatPrice(7)).toBe("0.07");
        expect(parsePrice("100.5")).toBe(10_050);
        expect(parsePrice(" 99.99 ")).toBe(9_999);
        expect(parsePrice("100")).toBe(10_000);
        for (const bad of ["", "0", "-1", "1.234", "1e3", "abc", "1,000"]) expect(parsePrice(bad)).toBeNull();
    });

    it("parses positive whole quantities only", () => {
        expect(parseQuantity("250")).toBe(250);
        for (const bad of ["0", "-5", "2.5", "x"]) expect(parseQuantity(bad)).toBeNull();
    });
});
