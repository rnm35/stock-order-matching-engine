// Measures the engine's cost per request in this JS runtime (browser or Node), including
// the JS/wasm boundary and decoding events into objects.
//
// Browsers deliberately coarsen performance.now() (often to 0.1 ms), far coarser than one
// request (~0.3 µs), so timing calls one by one would mostly measure clock rounding. Instead
// this times one batch of many requests against a scratch book.

import { Engine } from "./engine.ts";
import type { OmeModule } from "./wasm/ome.js";

export interface EngineSpeed {
    requests: number;
    microsPerRequest: number;
}

export function measureEngine(ome: OmeModule, adds = 75_000): EngineSpeed {
    const engine = new Engine(ome, "modern", 0);
    let id = 1;
    let requests = 0;
    const start = performance.now();
    for (let i = 0; i < adds; ++i) {
        // Bids 99.90-100.01 and asks 99.91-100.02: mostly resting orders, some crossing.
        const buy = (i & 1) === 1;
        engine.submitLimit(id++, buy ? "buy" : "sell", 10, buy ? 9_990 + (i % 12) : 10_002 - (i % 12));
        ++requests;
        if (i % 3 === 0) {
            engine.cancel(id - 1);
            ++requests;
        }
    }
    const millis = performance.now() - start;
    engine.dispose();
    return { requests, microsPerRequest: (millis * 1000) / requests };
}
