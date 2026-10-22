#!/usr/bin/env python3
"""Generate a random order file in the legacy input format.

Usage: python3 tools/gen_orders.py <num_orders> <output_file> [seed] [market_fraction] [price_sd]

Prices are Gaussian around 100.00 (sd `price_sd`, default 2), quantities 1-100, sides 50/50.
A fraction `market_fraction` (default 0) of orders are market orders (no price).
With the defaults this is the generator used for the baseline benchmark in PLAN.md (seed 1);
the extra arguments don't change its output when left at their defaults.
"""
import random
import sys


def generate(n, seed=1, market_fraction=0.0, price_sd=2.0):
    rng = random.Random(seed)
    lines = ["100.00"]
    for i in range(n):
        side = rng.choice("BS")
        qty = rng.randint(1, 100)
        price = round(rng.gauss(100, price_sd), 2)
        # Only draw the extra number when needed, so the default stream stays identical.
        if market_fraction > 0 and rng.random() < market_fraction:
            lines.append(f"o{i} {side} {qty}")
        else:
            lines.append(f"o{i} {side} {qty} {price}")
    return "\n".join(lines) + "\n"


if __name__ == "__main__":
    n = int(sys.argv[1])
    out = sys.argv[2]
    seed = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    market_fraction = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
    price_sd = float(sys.argv[5]) if len(sys.argv) > 5 else 2.0
    with open(out, "w", newline="\n") as f:
        f.write(generate(n, seed, market_fraction, price_sd))
