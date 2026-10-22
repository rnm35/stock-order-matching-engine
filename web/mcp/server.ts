// MCP server (stdio) exposing the order book to Model Context Protocol clients.
//
//   npm --prefix web run mcp          (needs the wasm build: cmake --preset wasm ...)
//
// Environment: OME_SEED (default 1), OME_MODE ("modern" | "legacy", default modern).
// Runs directly under Node >= 22.18, which strips TypeScript types itself.

import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";

import { Market } from "../src/sim/market.ts";
import { createOrderBookServer } from "./tools.ts";

const mode = process.env.OME_MODE === "legacy" ? "legacy" : "modern";
const seed = Number(process.env.OME_SEED ?? 1);

const market = await Market.create({ seed, mode });
market.step(3_000); // start with a populated book

const server = createOrderBookServer(market);
await server.connect(new StdioServerTransport());
// stdout carries the protocol; diagnostics go to stderr.
console.error(`order-book MCP server ready (${mode} mode, seed ${seed})`);
