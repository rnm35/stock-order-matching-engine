// Prices are integer ticks everywhere; these convert at the edges (inputs and display),
// mirroring ome::text::parsePrice / formatPrice in the C++ library.

export const TICKS_PER_UNIT = 100;

/** 10050 -> "100.50" */
export function formatPrice(ticks: number): string {
    const sign = ticks < 0 ? "-" : "";
    const abs = Math.abs(ticks);
    const cents = abs % TICKS_PER_UNIT;
    return `${sign}${Math.floor(abs / TICKS_PER_UNIT)}.${cents.toString().padStart(2, "0")}`;
}

/**
 * "100.5" -> 10050. Rejects anything that isn't a positive decimal with at most two decimal
 * places (no rounding, no floating point).
 */
export function parsePrice(text: string): number | null {
    const match = /^(\d{1,13})(?:\.(\d{0,2}))?$/.exec(text.trim());
    if (!match) return null;
    const whole = Number(match[1]);
    const cents = Number((match[2] ?? "").padEnd(2, "0"));
    const ticks = whole * TICKS_PER_UNIT + cents;
    return ticks > 0 ? ticks : null;
}

/** "250" -> 250; null unless a positive whole number. */
export function parseQuantity(text: string): number | null {
    if (!/^\d{1,12}$/.test(text.trim())) return null;
    const quantity = Number(text.trim());
    return quantity > 0 ? quantity : null;
}
