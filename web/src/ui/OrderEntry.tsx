import { useEffect, useState } from "react";
import type { FormEvent } from "react";

import type { Side, TimeInForce } from "../engine/engine.ts";
import { formatPrice, parsePrice, parseQuantity } from "../engine/price.ts";
import type { UserOrder } from "../sim/market.ts";

export interface OrderRequest {
    side: Side;
    orderType: "limit" | "market";
    quantity: number;
    price?: number;
    tif: TimeInForce;
}

interface Props {
    bestBid: number | null;
    bestAsk: number | null;
    /** Set when a ladder row is clicked. */
    pickedPrice: number | null;
    onSubmit: (request: OrderRequest) => UserOrder | null;
}

export function OrderEntry({ bestBid, bestAsk, pickedPrice, onSubmit }: Props) {
    const [side, setSide] = useState<Side>("buy");
    const [orderType, setOrderType] = useState<"limit" | "market">("limit");
    const [price, setPrice] = useState("");
    const [quantity, setQuantity] = useState("100");
    const [tif, setTif] = useState<TimeInForce>("GTC");
    const [result, setResult] = useState<{ text: string; tone: "ok" | "warn" } | null>(null);

    useEffect(() => {
        if (pickedPrice !== null) {
            setPrice(formatPrice(pickedPrice));
            setOrderType("limit");
        }
    }, [pickedPrice]);

    const touch = side === "buy" ? bestAsk : bestBid;
    // An empty price field means the price shown as its placeholder (the opposite best).
    const priceTicks = price.trim() === "" ? touch : parsePrice(price);
    const quantityValue = parseQuantity(quantity);
    const priceError = orderType === "limit" && priceTicks === null ? "Enter a price like 100.25" : null;
    const quantityError = quantityValue === null ? "Enter a whole number of shares" : null;

    const submit = (event: FormEvent) => {
        event.preventDefault();
        if (priceError || quantityError || quantityValue === null) return;
        const order = onSubmit({
            side,
            orderType,
            quantity: quantityValue,
            price: orderType === "limit" ? (priceTicks as number) : undefined,
            tif,
        });
        if (order) setResult(describe(order));
    };

    return (
        <form className="order-entry" onSubmit={submit}>
            <div className="segmented" role="group" aria-label="Side">
                <button type="button" className={side === "buy" ? "on buy" : ""} aria-pressed={side === "buy"} onClick={() => setSide("buy")}>
                    Buy
                </button>
                <button type="button" className={side === "sell" ? "on sell" : ""} aria-pressed={side === "sell"} onClick={() => setSide("sell")}>
                    Sell
                </button>
            </div>

            <div className="row">
                <label>
                    Type
                    <select value={orderType} onChange={(e) => setOrderType(e.target.value as "limit" | "market")}>
                        <option value="limit">Limit</option>
                        <option value="market">Market</option>
                    </select>
                </label>
                <label>
                    Time in force
                    <select value={tif} onChange={(e) => setTif(e.target.value as TimeInForce)}>
                        <option value="GTC">GTC</option>
                        <option value="IOC">IOC</option>
                        <option value="FOK">FOK</option>
                    </select>
                </label>
            </div>

            <div className="row">
                <label>
                    Price
                    <input
                        inputMode="decimal"
                        value={orderType === "market" ? "" : price}
                        placeholder={orderType === "market" ? "market" : touch === null ? "100.00" : formatPrice(touch)}
                        disabled={orderType === "market"}
                        onChange={(e) => setPrice(e.target.value)}
                        aria-invalid={priceError !== null && price !== ""}
                    />
                </label>
                <label>
                    Quantity
                    <input
                        inputMode="numeric"
                        value={quantity}
                        onChange={(e) => setQuantity(e.target.value)}
                        aria-invalid={quantityError !== null}
                    />
                </label>
            </div>

            {orderType === "limit" && touch !== null && (
                <button type="button" className="link" onClick={() => setPrice(formatPrice(touch))}>
                    Use best {side === "buy" ? "ask" : "bid"} ({formatPrice(touch)})
                </button>
            )}

            <button type="submit" className={`submit ${side}`} disabled={priceError !== null || quantityError !== null}>
                {side === "buy" ? "Buy" : "Sell"} {quantityValue?.toLocaleString() ?? ""}
                {orderType === "limit" && priceTicks !== null ? ` @ ${formatPrice(priceTicks)}` : orderType === "market" ? " at market" : ""}
            </button>

            <p className={`result ${result?.tone ?? ""}`} aria-live="polite">
                {result?.text ?? (priceError && price !== "" ? priceError : quantityError ?? "")}
            </p>
        </form>
    );
}

function describe(order: UserOrder): { text: string; tone: "ok" | "warn" } {
    const head = `#${order.id}`;
    switch (order.status) {
        case "filled":
            return { text: `${head} filled ${order.filled.toLocaleString()}`, tone: "ok" };
        case "open":
            return {
                text: order.filled > 0
                    ? `${head} filled ${order.filled.toLocaleString()}, ${order.remaining.toLocaleString()} resting`
                    : `${head} resting in the book`,
                tone: "ok",
            };
        case "cancelled":
            return {
                text: `${head} ${order.filled > 0 ? `filled ${order.filled.toLocaleString()}, rest ` : ""}cancelled (${order.note})`,
                tone: "warn",
            };
        case "rejected":
            return { text: `${head} rejected: ${order.note}`, tone: "warn" };
    }
}
