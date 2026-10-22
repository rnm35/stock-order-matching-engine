import { formatPrice } from "../engine/price.ts";
import type { TradePrint } from "../sim/market.ts";

/** Most recent trades first; the arrow shows the aggressor (buyer-initiated = up). */
export function TradeTape({ trades }: { trades: TradePrint[] }) {
    if (trades.length === 0) return <p className="empty">No trades yet.</p>;
    return (
        <ol className="tape">
            {trades.slice(0, 40).map((t) => (
                <li key={t.seq} className={`${t.aggressor === "buy" ? "bid" : "ask"}${t.userSide ? " mine" : ""}`}>
                    <span className="arrow" aria-label={t.aggressor === "buy" ? "buyer initiated" : "seller initiated"}>
                        {t.aggressor === "buy" ? "▲" : "▼"}
                    </span>
                    <span className="price">{formatPrice(t.price)}</span>
                    <span className="qty">{t.quantity.toLocaleString()}</span>
                    <span className="tag">{t.userSide ? `you ${t.userSide === "buy" ? "bought" : "sold"}` : ""}</span>
                </li>
            ))}
        </ol>
    );
}
