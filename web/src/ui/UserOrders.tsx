import { formatPrice } from "../engine/price.ts";
import type { UserOrder } from "../sim/market.ts";

interface Props {
    orders: UserOrder[];
    onCancel: (id: number) => void;
}

export function UserOrders({ orders, onCancel }: Props) {
    if (orders.length === 0) {
        return <p className="empty">No orders yet. Place one on the right, or click a price in the book.</p>;
    }
    return (
        <div className="table-scroll">
            <table className="orders">
                <thead>
                    <tr>
                        <th>#</th>
                        <th>Side</th>
                        <th>Price</th>
                        <th className="num">Qty</th>
                        <th className="num">Filled</th>
                        <th className="num">Open</th>
                        <th>Status</th>
                        <th aria-label="Actions" />
                    </tr>
                </thead>
                <tbody>
                    {orders.slice(0, 30).map((o) => (
                        <tr key={o.id} className={o.status}>
                            <td>{o.id}</td>
                            <td className={o.side === "buy" ? "bid" : "ask"}>{o.side}</td>
                            <td>{o.price === null ? "market" : formatPrice(o.price)} <span className="muted">{o.tif}</span></td>
                            <td className="num">{o.quantity.toLocaleString()}</td>
                            <td className="num">{o.filled.toLocaleString()}</td>
                            <td className="num">{o.remaining.toLocaleString()}</td>
                            <td>
                                {o.status}
                                {o.note && <span className="muted"> · {o.note}</span>}
                            </td>
                            <td>
                                {o.status === "open" && (
                                    <button type="button" className="small" onClick={() => onCancel(o.id)}>
                                        Cancel
                                    </button>
                                )}
                            </td>
                        </tr>
                    ))}
                </tbody>
            </table>
        </div>
    );
}
