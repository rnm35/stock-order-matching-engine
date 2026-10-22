#!/usr/bin/env python3
"""Regenerate the golden test files in tests/golden/.

The expected outputs come from the ORIGINAL coursework program, not from the new engine.
It is read from git history (ORIGINAL below: main/main.cpp in the repository's original
commit), so this script needs a clone with full history. Only bug 1 from PLAN.md is fixed:
  - same-price ties broken by arrival order instead of comparing order ids as strings;
  - a partially filled resting order keeps its place instead of moving to the back.
Bugs 2-4 don't affect the output file's contents, so nothing else is changed.

Usage: python3 tools/make_golden.py [--cxx g++]

1. Writes the generated inputs (GENERATED below) into tests/golden/.
2. Patches + compiles the original into a temp directory.
3. Runs it on every tests/golden/*.txt (hand-written ones included) and writes
   <name>.expected.txt next to each input.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOLDEN = ROOT / "tests" / "golden"
sys.path.insert(0, str(ROOT / "tools"))
from gen_orders import generate  # noqa: E402

# The coursework program as it was first published, before any of this project's changes.
ORIGINAL = "546e7587f30b79a8a7dc157d34f1cfa767b9b811:main/main.cpp"

# name: (orders, seed, market_fraction, price_sd)
GENERATED = {
    "gen_limit_only": (500, 1, 0.0, 2.0),      # the benchmark distribution
    "gen_heavy_ties": (400, 2, 0.0, 0.3),      # most orders share a handful of prices
    "gen_market_10pct": (400, 3, 0.1, 0.5),
    "gen_market_30pct": (400, 4, 0.3, 0.5),
    "gen_market_60pct": (300, 5, 0.6, 1.0),    # lots of market-vs-market trades
}

# (old, new) text replacements applied to the original source. Each must match exactly once.
# The partial-fill fix is a span replacement instead (SPAN_* below), because the original
# has trailing whitespace inside that block.
PATCHES = [
    ("#include <limits>",
     "#include <limits>\n#include <unordered_map>\n"
     "std::unordered_map<std::string, size_t> arrivalIndex;  // golden-test patch"),
    ("(effective == match.bestPrice && it->orderID < match.bestMatch->orderID)",
     "(effective == match.bestPrice && "
     "arrivalIndex[it->orderID] < arrivalIndex[match.bestMatch->orderID])"),
    ("    std::vector<Order> ordersToProcess = pendingOrders;\n",
     "    std::vector<Order> ordersToProcess = pendingOrders;\n"
     "    for (size_t k = 0; k < ordersToProcess.size(); ++k) "
     "arrivalIndex[ordersToProcess[k].orderID] = k;\n"),
]

# Everything from SPAN_START up to (not including) SPAN_END is replaced by SPAN_NEW: the
# original erased the matched order and pushed any remainder to the back of the queue.
SPAN_START = "            Order matchedOrder = *match.bestMatch;"
SPAN_END = "        } else {\n            //exit if no valid match"
SPAN_NEW = """            Order& matchedOrder = *match.bestMatch;  // golden-test patch: update in place
            if (match.isBuyOrder) executeOrders(newOrder, matchedOrder, allOrders);
            else executeOrders(matchedOrder, newOrder, allOrders);
            if (matchedOrder.quantity == 0) pendingOrders.erase(match.bestMatch);
"""


def read_original():
    """The original program's source, exactly as committed before the rebuild."""
    try:
        return subprocess.run(["git", "show", ORIGINAL], cwd=ROOT, check=True,
                              capture_output=True, text=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        sys.exit(f"cannot read {ORIGINAL} from git history (needs a full clone): {e}")


def build_oracle(cxx, workdir):
    src = read_original()
    for old, new in PATCHES:
        if src.count(old) != 1:
            sys.exit(f"patch anchor not found exactly once:\n{old}")
        src = src.replace(old, new)
    if src.count(SPAN_START) != 1 or src.count(SPAN_END) != 1:
        sys.exit("partial-fill patch anchors not found exactly once")
    start, end = src.index(SPAN_START), src.index(SPAN_END)
    src = src[:start] + SPAN_NEW + src[end:]
    cpp = workdir / "main_fixed.cpp"
    exe = workdir / ("main_fixed.exe" if os.name == "nt" else "main_fixed")
    cpp.write_text(src)
    subprocess.run([cxx, "-O2", "-std=c++17", str(cpp), "-o", str(exe)]
                   + (["-static"] if os.name == "nt" else []), check=True)
    return exe


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cxx", default=os.environ.get("CXX", "g++"))
    args = ap.parse_args()

    GOLDEN.mkdir(parents=True, exist_ok=True)
    for name, (n, seed, mkt, sd) in GENERATED.items():
        (GOLDEN / f"{name}.txt").write_text(generate(n, seed, mkt, sd), newline="\n")

    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        exe = build_oracle(args.cxx, tmp)
        inputs = sorted(p for p in GOLDEN.glob("*.txt") if not p.name.endswith(".expected.txt"))
        for inp in inputs:
            # The original names its output after the first digit in the input name and
            # writes it to the current directory.
            run_dir = tmp / "run"
            shutil.rmtree(run_dir, ignore_errors=True)
            run_dir.mkdir()
            shutil.copy(inp, run_dir / "input1.txt")
            subprocess.run([str(exe), "input1.txt"], cwd=run_dir, check=True,
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            out = (run_dir / "output1.txt").read_text().replace("\r\n", "\n")
            (GOLDEN / f"{inp.stem}.expected.txt").write_text(out, newline="\n")
            print(f"{inp.name}: {out.count(chr(10))} lines")


if __name__ == "__main__":
    main()
