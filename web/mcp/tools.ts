// MCP tools over a simulated market (the same Market the web UI uses, with the C++ engine
// running as WebAssembly). The connected client trades as the "user"; bots
// provide liquidity. Prices cross the protocol as decimal strings ("100.25") and are
// converted to integer ticks exactly, never through floating point.

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { z } from "zod";

import { formatPrice, parsePrice } from "../src/engine/price.ts";
import type { Level } from "../src/engine/engine.ts";
import type { Market, UserOrder } from "../src/sim/market.ts";

type ToolResult = { content: { type: "text"; text: string }[]; isError?: boolean };

const ok = (value: unknown): ToolResult => ({ content: [{ type: "text", text: JSON.stringify(value, null, 2) }] });
const fail = (message: string): ToolResult => ({ content: [{ type: "text", text: message }], isError: true });

const levels = (ls: Level[]) => ls.map((l) => ({ price: formatPrice(l.price), quantity: l.quantity, orders: l.orders }));
const price = (ticks: number | null) => (ticks === null ? null : formatPrice(ticks));

function describeOrder(o: UserOrder) {
    return {
        id: o.id,
        side: o.side,
        type: o.orderType,
        price: price(o.price),
        time_in_force: o.tif,
        quantity: o.quantity,
        filled: o.filled,
        remaining: o.remaining,
        status: o.status,
        ...(o.note ? { note: o.note } : {}),
    };
}

export function createOrderBookServer(market: Market): McpServer {
    const server = new McpServer({ name: "order-book", version: "0.1.0" });

    server.registerTool(
        "get_book",
        {
            title: "Get order book",
            description:
                "Current limit order book: best bid/ask, spread, last trade price and the top price levels on each side (price, total quantity, number of orders), best first.",
            inputSchema: { levels: z.number().int().min(1).max(50).default(10).describe("Price levels per side") },
        },
        async ({ levels: n }) => {
            const s = market.snapshot(n, 0);
            return ok({
                mode: market.engine.mode,
                best_bid: price(s.bestBid),
                best_ask: price(s.bestAsk),
                spread: s.bestBid !== null && s.bestAsk !== null ? formatPrice(s.bestAsk - s.bestBid) : null,
                last_trade: price(s.lastPrice),
                resting_orders: s.restingOrders,
                bids: levels(s.bids),
                asks: levels(s.asks),
            });
        },
    );

    server.registerTool(
        "place_order",
        {
            title: "Place order",
            description:
                "Submit an order. Limit orders need a price (decimal string, at most 2 decimals). Market orders take the best available prices; in modern mode any unfilled remainder is cancelled. time_in_force: GTC rests the remainder, IOC cancels it, FOK fills completely or not at all. Returns the order's state and the trades it caused.",
            inputSchema: {
                side: z.enum(["buy", "sell"]),
                type: z.enum(["limit", "market"]),
                quantity: z.number().int().positive().describe("Shares"),
                price: z.string().optional().describe('Limit price, e.g. "100.25"'),
                time_in_force: z.enum(["GTC", "IOC", "FOK"]).default("GTC"),
            },
        },
        async (args) => {
            let ticks: number | undefined;
            if (args.type === "limit") {
                const parsed = args.price === undefined ? null : parsePrice(args.price);
                if (parsed === null) return fail('A limit order needs a positive price with at most 2 decimals, e.g. "100.25".');
                ticks = parsed;
            }
            const order = market.placeUserOrder({
                side: args.side,
                orderType: args.type,
                quantity: args.quantity,
                price: ticks,
                tif: args.time_in_force,
            });
            const fills = market
                .snapshot(1, 500)
                .tape.filter((t) => t.buyId === order.id || t.sellId === order.id)
                .reverse()
                .map((t) => ({ price: formatPrice(t.price), quantity: t.quantity }));
            const filledValue = fills.reduce((sum, f) => sum + (parsePrice(f.price) as number) * f.quantity, 0);
            return ok({
                order: describeOrder(order),
                fills,
                average_fill_price: order.filled > 0 ? (filledValue / order.filled / 100).toFixed(4) : null,
            });
        },
    );

    server.registerTool(
        "cancel_order",
        {
            title: "Cancel order",
            description: "Cancel one of your resting orders by id.",
            inputSchema: { order_id: z.number().int().positive() },
        },
        async ({ order_id }) => {
            const before = market.userOrder(order_id);
            if (!before) return fail(`No order #${order_id} was placed in this session.`);
            if (before.status !== "open") return fail(`Order #${order_id} is already ${before.status}.`);
            market.cancelUserOrder(order_id);
            return ok({ order: describeOrder(market.userOrder(order_id) as UserOrder) });
        },
    );

    server.registerTool(
        "list_orders",
        {
            title: "List my orders",
            description: "All orders placed in this session, newest first, with fills and status.",
            inputSchema: {},
        },
        async () => ok({ orders: market.snapshot(1, 0).userOrders.map(describeOrder) }),
    );

    server.registerTool(
        "get_trades",
        {
            title: "Recent trades",
            description: "Most recent trades first: price, quantity, which side was the aggressor, and whether you were involved.",
            inputSchema: { limit: z.number().int().min(1).max(200).default(20) },
        },
        async ({ limit }) =>
            ok({
                trades: market.snapshot(1, limit).tape.map((t) => ({
                    price: formatPrice(t.price),
                    quantity: t.quantity,
                    aggressor: t.aggressor,
                    ...(t.userSide ? { you: t.userSide === "buy" ? "bought" : "sold" } : {}),
                })),
            }),
    );

    server.registerTool(
        "run_simulation",
        {
            title: "Run the market",
            description:
                "Let the simulated traders (two market makers and noise traders) act, moving the market. Useful between your own orders.",
            inputSchema: { actions: z.number().int().min(1).max(100_000).default(1_000) },
        },
        async ({ actions }) => {
            const before = { ...market.stats };
            market.step(actions);
            const s = market.snapshot(1, 0);
            return ok({
                requests: market.stats.requests - before.requests,
                trades: market.stats.trades - before.trades,
                volume: market.stats.volume - before.volume,
                best_bid: price(s.bestBid),
                best_ask: price(s.bestAsk),
                last_trade: price(s.lastPrice),
            });
        },
    );

    return server;
}
