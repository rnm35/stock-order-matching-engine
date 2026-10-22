import { useMemo, useState } from "react";
import type { MouseEvent } from "react";

import type { Level } from "../engine/engine.ts";
import { formatPrice } from "../engine/price.ts";

const WIDTH = 640;
const HEIGHT = 260;
const PAD = { left: 52, right: 32, top: 12, bottom: 28 };

interface Props {
    bids: Level[];
    asks: Level[];
    userPrices: Set<number>;
}

interface Point {
    price: number;
    cumulative: number;
}

function cumulative(levels: Level[]): Point[] {
    let total = 0;
    return levels.map((l) => ({ price: l.price, cumulative: (total += l.quantity) }));
}

/**
 * Cumulative depth: how many shares you could buy (asks, right) or sell (bids, left) down to
 * each price. Drawn as step areas, which is what the book really is (depth only changes at
 * price levels).
 */
export function DepthChart({ bids, asks, userPrices }: Props) {
    const [hover, setHover] = useState<number | null>(null);
    const bidPoints = useMemo(() => cumulative(bids), [bids]);
    const askPoints = useMemo(() => cumulative(asks), [asks]);

    if (bidPoints.length === 0 && askPoints.length === 0) {
        return <div className="empty">The book is empty.</div>;
    }

    const lowest = bidPoints.at(-1)?.price ?? askPoints[0]?.price ?? 0;
    const highest = askPoints.at(-1)?.price ?? bidPoints[0]?.price ?? 0;
    const minPrice = lowest - 1;
    const maxPrice = highest + 1;
    const maxCumulative = Math.max(1, bidPoints.at(-1)?.cumulative ?? 0, askPoints.at(-1)?.cumulative ?? 0) * 1.08;

    const x = (price: number) => PAD.left + ((price - minPrice) / (maxPrice - minPrice)) * (WIDTH - PAD.left - PAD.right);
    const y = (quantity: number) => HEIGHT - PAD.bottom - (quantity / maxCumulative) * (HEIGHT - PAD.top - PAD.bottom);
    const baseline = y(0);

    // Bids: starting at the best bid and stepping down (left) in price.
    const bidPath = (() => {
        if (bidPoints.length === 0) return "";
        let d = `M ${x((bidPoints[0] as Point).price)} ${baseline}`;
        let previous = 0;
        for (const p of bidPoints) {
            d += ` L ${x(p.price)} ${y(previous)} L ${x(p.price)} ${y(p.cumulative)}`;
            previous = p.cumulative;
        }
        return `${d} L ${x(minPrice)} ${y(previous)} L ${x(minPrice)} ${baseline} Z`;
    })();
    const askPath = (() => {
        if (askPoints.length === 0) return "";
        let d = `M ${x((askPoints[0] as Point).price)} ${baseline}`;
        let previous = 0;
        for (const p of askPoints) {
            d += ` L ${x(p.price)} ${y(previous)} L ${x(p.price)} ${y(p.cumulative)}`;
            previous = p.cumulative;
        }
        return `${d} L ${x(maxPrice)} ${y(previous)} L ${x(maxPrice)} ${baseline} Z`;
    })();

    const priceTicks = Array.from({ length: 5 }, (_, i) => Math.round(minPrice + ((maxPrice - minPrice) * i) / 4));
    const quantityTicks = Array.from({ length: 4 }, (_, i) => Math.round((maxCumulative * (i + 1)) / 4));
    const mid =
        bidPoints[0] && askPoints[0] ? (bidPoints[0].price + askPoints[0].price) / 2 : null;

    // Hover readout: cumulative depth available at the hovered price.
    let readout: { price: number; side: "bid" | "ask"; cumulative: number } | null = null;
    if (hover !== null) {
        if (mid !== null && hover >= mid) {
            const reached = askPoints.filter((p) => p.price <= hover);
            readout = { price: hover, side: "ask", cumulative: reached.at(-1)?.cumulative ?? 0 };
        } else {
            const reached = bidPoints.filter((p) => p.price >= hover);
            readout = { price: hover, side: "bid", cumulative: reached.at(-1)?.cumulative ?? 0 };
        }
    }

    const onMove = (event: MouseEvent<SVGSVGElement>) => {
        const rect = event.currentTarget.getBoundingClientRect();
        const px = ((event.clientX - rect.left) / rect.width) * WIDTH;
        const price = minPrice + ((px - PAD.left) / (WIDTH - PAD.left - PAD.right)) * (maxPrice - minPrice);
        setHover(price < minPrice || price > maxPrice ? null : Math.round(price));
    };

    return (
        <svg
            className="depth-chart"
            viewBox={`0 0 ${WIDTH} ${HEIGHT}`}
            role="img"
            aria-label="Cumulative depth chart"
            onMouseMove={onMove}
            onMouseLeave={() => setHover(null)}
        >
            {quantityTicks.map((q) => (
                <g key={`q${q}`}>
                    <line className="gridline" x1={PAD.left} x2={WIDTH - PAD.right} y1={y(q)} y2={y(q)} />
                    <text className="axis" x={PAD.left - 6} y={y(q) + 4} textAnchor="end">
                        {q >= 10_000 ? `${Math.round(q / 1000)}k` : q.toLocaleString()}
                    </text>
                </g>
            ))}
            <path className="area bid" d={bidPath} />
            <path className="area ask" d={askPath} />
            {[...userPrices].map((p) => (
                <line key={`u${p}`} className="user-line" x1={x(p)} x2={x(p)} y1={PAD.top} y2={baseline} />
            ))}
            {mid !== null && <line className="mid-line" x1={x(mid)} x2={x(mid)} y1={PAD.top} y2={baseline} />}
            <line className="axis-line" x1={PAD.left} x2={WIDTH - PAD.right} y1={baseline} y2={baseline} />
            {priceTicks.map((p) => (
                <text key={`p${p}`} className="axis" x={x(p)} y={HEIGHT - 8} textAnchor="middle">
                    {formatPrice(p)}
                </text>
            ))}
            {readout && (
                <g className="readout">
                    <line x1={x(readout.price)} x2={x(readout.price)} y1={PAD.top} y2={baseline} />
                    <text
                        x={Math.min(x(readout.price) + 6, WIDTH - 150)}
                        y={PAD.top + 14}
                        className={readout.side}
                    >
                        {`${formatPrice(readout.price)}: ${readout.cumulative.toLocaleString()} to ${readout.side === "ask" ? "buy" : "sell"}`}
                    </text>
                </g>
            )}
        </svg>
    );
}
