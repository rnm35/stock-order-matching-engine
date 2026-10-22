import type { Level } from "../engine/engine.ts";
import { formatPrice } from "../engine/price.ts";

const ROWS_PER_SIDE = 12;

interface Props {
    bids: Level[];
    asks: Level[];
    userPrices: Set<number>;
    onPick: (price: number) => void;
}

/** Price ladder: asks above (best at the bottom), bids below (best at the top). */
export function Ladder({ bids, asks, userPrices, onPick }: Props) {
    const shownAsks = asks.slice(0, ROWS_PER_SIDE).reverse();
    const shownBids = bids.slice(0, ROWS_PER_SIDE);
    const maxQuantity = Math.max(1, ...shownAsks.map((l) => l.quantity), ...shownBids.map((l) => l.quantity));
    const bestBid = bids[0]?.price;
    const bestAsk = asks[0]?.price;

    const row = (level: Level, side: "bid" | "ask") => (
        <tr
            key={`${side}${level.price}`}
            className={`ladder-row ${side}`}
            onClick={() => onPick(level.price)}
            title="Click to use this price"
        >
            <td className="price">
                {userPrices.has(level.price) && <span className="mine-dot" aria-label="your order" />}
                {formatPrice(level.price)}
            </td>
            <td className="qty">
                <span className="bar" style={{ width: `${(level.quantity / maxQuantity) * 100}%` }} />
                <span className="num">{level.quantity.toLocaleString()}</span>
            </td>
            <td className="count">{level.orders}</td>
        </tr>
    );

    return (
        <table className="ladder">
            <thead>
                <tr>
                    <th>Price</th>
                    <th className="qty">Size</th>
                    <th className="count">Orders</th>
                </tr>
            </thead>
            <tbody>
                {shownAsks.map((l) => row(l, "ask"))}
                <tr className="spread-row">
                    <td colSpan={3}>
                        {bestBid !== undefined && bestAsk !== undefined
                            ? `spread ${formatPrice(bestAsk - bestBid)}`
                            : "one side empty"}
                    </td>
                </tr>
                {shownBids.map((l) => row(l, "bid"))}
            </tbody>
        </table>
    );
}
