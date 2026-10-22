import { useCallback, useEffect, useRef, useState } from "react";

import { measureEngine } from "../engine/benchmark.ts";
import type { EngineSpeed } from "../engine/benchmark.ts";
import { loadEngine } from "../engine/engine.ts";
import type { Mode } from "../engine/engine.ts";
import { formatPrice } from "../engine/price.ts";
import { Market } from "../sim/market.ts";
import type { Snapshot } from "../sim/market.ts";
import { Controls } from "./Controls.tsx";
import { DepthChart } from "./DepthChart.tsx";
import { Ladder } from "./Ladder.tsx";
import { OrderEntry } from "./OrderEntry.tsx";
import type { OrderRequest } from "./OrderEntry.tsx";
import { TradeTape } from "./TradeTape.tsx";
import { UserOrders } from "./UserOrders.tsx";

const LEVELS = 25;
const WARM_UP_ACTIONS = 3_000;


export function App() {
    const marketRef = useRef<Market | null>(null);
    const [snapshot, setSnapshot] = useState<Snapshot | null>(null);
    const [mode, setMode] = useState<Mode>("modern");
    const [seed, setSeed] = useState(1);
    const [running, setRunning] = useState(true);
    const [speed, setSpeed] = useState(300); // bot actions per second
    const [requestsPerSecond, setRequestsPerSecond] = useState(0);
    const [engineSpeed, setEngineSpeed] = useState<EngineSpeed | null>(null);
    const [pickedPrice, setPickedPrice] = useState<number | null>(null);
    const [error, setError] = useState<string | null>(null);

    const refresh = useCallback(() => {
        if (marketRef.current) setSnapshot(marketRef.current.snapshot(LEVELS));
    }, []);

    // (Re)create the market when the mode or seed changes.
    useEffect(() => {
        let cancelled = false;
        let created: Market | null = null;
        Market.create({ seed, mode })
            .then((m) => {
                if (cancelled) {
                    m.dispose();
                    return;
                }
                created = m;
                m.step(WARM_UP_ACTIONS); // start with a populated book
                marketRef.current = m;
                setSnapshot(m.snapshot(LEVELS));
            })
            .catch((e: unknown) => setError(e instanceof Error ? e.message : String(e)));
        return () => {
            cancelled = true;
            if (created) {
                marketRef.current = null;
                created.dispose();
            }
        };
    }, [mode, seed]);

    // Simulation loop: run bots at `speed` actions/second, redraw every animation frame.
    useEffect(() => {
        if (!running) return;
        let frame = 0;
        let last = performance.now();
        let carry = 0;
        const tick = (now: number) => {
            const m = marketRef.current;
            if (m) {
                carry += (speed * Math.min(now - last, 250)) / 1000;
                const actions = Math.floor(carry);
                carry -= actions;
                m.step(actions);
                setSnapshot(m.snapshot(LEVELS));
            }
            last = now;
            frame = requestAnimationFrame(tick);
        };
        frame = requestAnimationFrame(tick);
        return () => cancelAnimationFrame(frame);
    }, [running, speed]);

    // Requests the simulation is sending per second, over the last second.
    useEffect(() => {
        let previous = { at: performance.now(), requests: -1 };
        const timer = setInterval(() => {
            const m = marketRef.current;
            if (!m) return;
            const now = performance.now();
            if (previous.requests >= 0) {
                setRequestsPerSecond(((m.stats.requests - previous.requests) * 1000) / (now - previous.at));
            }
            previous = { at: now, requests: m.stats.requests };
        }, 1000);
        return () => clearInterval(timer);
    }, [mode, seed]);

    // How fast the engine itself is in this browser (see engine/benchmark.ts).
    const runBenchmark = useCallback(() => {
        void loadEngine().then((ome) => setEngineSpeed(measureEngine(ome)));
    }, []);
    useEffect(() => {
        const timer = setTimeout(runBenchmark, 500); // after the first paint
        return () => clearTimeout(timer);
    }, [runBenchmark]);

    const placeOrder = useCallback(
        (request: OrderRequest) => {
            const m = marketRef.current;
            if (!m) return null;
            const order = m.placeUserOrder(request);
            refresh();
            return order;
        },
        [refresh],
    );

    const cancelOrder = useCallback(
        (id: number) => {
            marketRef.current?.cancelUserOrder(id);
            refresh();
        },
        [refresh],
    );

    if (error) return <div className="fatal">Could not start the engine: {error}</div>;
    if (!snapshot) return <div className="loading">Loading the matching engine…</div>;

    const market = marketRef.current;
    const spread =
        snapshot.bestBid !== null && snapshot.bestAsk !== null ? snapshot.bestAsk - snapshot.bestBid : null;
    const userPrices = new Set(
        snapshot.userOrders.filter((o) => o.status === "open" && o.price !== null).map((o) => o.price as number),
    );

    return (
        <div className="app">
            <header className="topbar">
                <div className="brand">
                    <h1>Order Book Live</h1>
                    <span className="subtitle">C++20 matching engine in WebAssembly · {mode} rules</span>
                </div>
                <dl className="stats">
                    <Stat label="Last" value={snapshot.lastPrice === null ? "–" : formatPrice(snapshot.lastPrice)} />
                    <Stat label="Bid" value={snapshot.bestBid === null ? "–" : formatPrice(snapshot.bestBid)} tone="bid" />
                    <Stat label="Ask" value={snapshot.bestAsk === null ? "–" : formatPrice(snapshot.bestAsk)} tone="ask" />
                    <Stat label="Spread" value={spread === null ? "–" : formatPrice(spread)} />
                    <Stat label="Resting" value={snapshot.restingOrders.toLocaleString()} />
                    <Stat label="Requests/s" value={Math.round(requestsPerSecond).toLocaleString()} />
                    <Stat
                        label="Engine µs/req"
                        value={engineSpeed ? engineSpeed.microsPerRequest.toFixed(2) : "–"}
                        title={
                            engineSpeed
                                ? `Measured in this browser: ${engineSpeed.requests.toLocaleString()} add/cancel requests timed as one batch, including the JS/wasm call and event decoding`
                                : "Measuring…"
                        }
                    />
                </dl>
            </header>

            <main className="grid">
                <section className="panel ladder-panel" aria-label="Order book ladder">
                    <h2>Order book</h2>
                    <Ladder bids={snapshot.bids} asks={snapshot.asks} userPrices={userPrices} onPick={setPickedPrice} />
                </section>

                <section className="panel chart-panel" aria-label="Depth chart">
                    <h2>
                        Depth <span className="hint">cumulative shares by price</span>
                    </h2>
                    <DepthChart bids={snapshot.bids} asks={snapshot.asks} userPrices={userPrices} />
                </section>

                <section className="panel orders-panel" aria-label="Your orders">
                    <h2>Your orders</h2>
                    <UserOrders orders={snapshot.userOrders} onCancel={cancelOrder} />
                </section>

                <aside className="side">
                    <section className="panel" aria-label="Order entry">
                        <h2>New order</h2>
                        <OrderEntry
                            bestBid={snapshot.bestBid}
                            bestAsk={snapshot.bestAsk}
                            pickedPrice={pickedPrice}
                            onSubmit={placeOrder}
                        />
                    </section>

                    <section className="panel" aria-label="Simulation controls">
                        <h2>Simulation</h2>
                        {market && (
                            <Controls
                                running={running}
                                onToggleRunning={() => setRunning((r) => !r)}
                                speed={speed}
                                onSpeed={setSpeed}
                                bots={market.bots}
                                onBotsChanged={refresh}
                                mode={mode}
                                onReset={(nextMode) => {
                                    setMode(nextMode);
                                    setSeed((s) => s + 1);
                                }}
                                onBurst={() => {
                                    market.step(10_000);
                                    refresh();
                                }}
                                stats={snapshot.stats}
                                onBenchmark={runBenchmark}
                            />
                        )}
                    </section>

                    <section className="panel tape-panel" aria-label="Trade tape">
                        <h2>Trades</h2>
                        <TradeTape trades={snapshot.tape} />
                    </section>
                </aside>
            </main>
        </div>
    );
}

function Stat(props: { label: string; value: string; tone?: "bid" | "ask"; title?: string }) {
    return (
        <div className="stat" title={props.title}>
            <dt>{props.label}</dt>
            <dd className={props.tone}>{props.value}</dd>
        </div>
    );
}
