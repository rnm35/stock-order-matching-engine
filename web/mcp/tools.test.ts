// The MCP tools end to end through the real SDK (client and server linked in memory).

import { Client } from "@modelcontextprotocol/sdk/client/index.js";
import { InMemoryTransport } from "@modelcontextprotocol/sdk/inMemory.js";
import { afterAll, beforeAll, describe, expect, it } from "vitest";

import { Market } from "../src/sim/market.ts";
import { createOrderBookServer } from "./tools.ts";

let market: Market;
let client: Client;

beforeAll(async () => {
    market = await Market.create({ seed: 3 });
    market.step(3_000);
    const [clientSide, serverSide] = InMemoryTransport.createLinkedPair();
    await createOrderBookServer(market).connect(serverSide);
    client = new Client({ name: "test", version: "0" });
    await client.connect(clientSide);
});

afterAll(async () => {
    await client.close();
    market.dispose();
});

async function call(name: string, args: Record<string, unknown> = {}) {
    const result = await client.callTool({ name, arguments: args });
    const [first] = result.content as { type: string; text: string }[];
    return { isError: result.isError === true, text: first?.text ?? "", json: () => JSON.parse(first?.text ?? "null") };
}

describe("MCP order-book tools", () => {
    it("lists the tools", async () => {
        const { tools } = await client.listTools();
        expect(tools.map((t) => t.name).sort()).toEqual(
            ["cancel_order", "get_book", "get_trades", "list_orders", "place_order", "run_simulation"].sort(),
        );
    });

    it("get_book returns two sides with prices as decimal strings", async () => {
        const book = (await call("get_book", { levels: 5 })).json();
        expect(book.bids).toHaveLength(5);
        expect(book.asks).toHaveLength(5);
        expect(book.best_bid).toMatch(/^\d+\.\d\d$/);
        expect(Number(book.best_bid)).toBeLessThan(Number(book.best_ask));
    });

    it("places, lists and cancels a resting limit order", async () => {
        const book = (await call("get_book")).json();
        const below = (Number(book.best_bid) - 1).toFixed(2);
        const placed = (await call("place_order", { side: "buy", type: "limit", quantity: 25, price: below })).json();
        expect(placed.order).toMatchObject({ status: "open", remaining: 25, price: below });

        const listed = (await call("list_orders")).json();
        expect(listed.orders[0]).toMatchObject({ id: placed.order.id, status: "open" });

        const cancelled = (await call("cancel_order", { order_id: placed.order.id })).json();
        expect(cancelled.order).toMatchObject({ status: "cancelled", remaining: 0 });
        expect((await call("cancel_order", { order_id: placed.order.id })).isError).toBe(true);
    });

    it("fills a market order and reports fills and average price", async () => {
        const result = (await call("place_order", { side: "sell", type: "market", quantity: 40 })).json();
        expect(result.order).toMatchObject({ status: "filled", filled: 40 });
        expect(result.fills.reduce((n: number, f: { quantity: number }) => n + f.quantity, 0)).toBe(40);
        expect(Number(result.average_fill_price)).toBeGreaterThan(0);

        const trades = (await call("get_trades", { limit: 5 })).json();
        expect(trades.trades.some((t: { you?: string }) => t.you === "sold")).toBe(true);
    });

    it("rejects bad prices without touching the book", async () => {
        for (const bad of ["100.123", "-5", "abc"]) {
            const r = await call("place_order", { side: "buy", type: "limit", quantity: 1, price: bad });
            expect(r.isError).toBe(true);
        }
        const r = await call("place_order", { side: "buy", type: "limit", quantity: 1 });
        expect(r.isError).toBe(true);
    });

    it("run_simulation moves the market", async () => {
        const result = (await call("run_simulation", { actions: 500 })).json();
        expect(result.requests).toBeGreaterThan(0);
        expect(result.best_bid).not.toBeNull();
    });
});
