# stock-order-matching-engine

A C++ command-line stock order matching engine that reads buy/sell orders from a file, matches them using price-time style rules, tracks the last traded price, and writes execution results to an output file.

## Project Overview

This project simulates a simplified exchange matching process:

- Reads an initial **last traded price** and a stream of orders.
- Supports both **limit orders** and **market orders**.
- Matches incoming orders against previously received opposite-side orders.
- Executes partial fills when quantities differ.
- Logs executed and unexecuted order summaries to an output file.
- Displays the pending order book in the console after each processed order.

## Features

- Buy (`B`) and Sell (`S`) order support.
- Market order handling (price omitted in input).
- Best-price-first matching:
  - For incoming **buy** orders: prefers lower sell prices.
  - For incoming **sell** orders: prefers higher buy prices.
- Price compatibility checks for limit orders.
- Execution price rules for market-vs-limit and limit-vs-limit combinations.
- Remaining quantity persistence for partially matched orders.

## Repository Structure

The matching engine implementation is in:

- `main/main.cpp` - full order parsing, matching, display, and output logic.
- `main/makefile` - build script for the `main` executable.

Additional files:

- `JumpList.h`
- `JumpList.cpp`

These `JumpList` files are not used by the stock matching executable in `main/main.cpp`.

## Requirements

- C++ compiler with C++11 support (e.g., `g++`).
- `make` (optional, if you use the provided makefile).

## Build

From the `main` directory:

```bash
make
```

This produces an executable named `main`.

### Alternative manual compile

```bash
g++ -Wall -Wextra -std=c++11 main.cpp -o main
```

## Run

From the `main` directory:

```bash
./main <input_file>
```

Example:

```bash
./main input1.txt
```

## Input File Format

The input file format is:

1. **First line**: last traded price (decimal number)
2. **Each next line**: one order in the form:

```text
<order_id> <type> <quantity> [price]
```

Where:

- `<order_id>`: unique order identifier (string)
- `<type>`: `B` for buy or `S` for sell
- `<quantity>`: positive integer
- `[price]`: optional decimal price
  - If omitted, the order is treated as a **market order**

### Example Input

```text
100.50
o1 B 100 101.25
o2 S 50 100.75
o3 S 100
o4 B 40
```

## Matching Behavior

The engine processes orders in arrival order and, for each order, repeatedly attempts matches until:

- The order is fully filled, or
- No valid opposite-side match remains.

### Opposite-side eligibility

An incoming order only matches against opposite-side orders that arrived earlier.

### Price acceptance

- Incoming **buy** limit order accepts sell orders priced `<= buy_price`.
- Incoming **sell** limit order accepts buy orders priced `>= sell_price`.
- Incoming market orders accept any price-compatible opposite order.

### Execution price rules

- **Market vs Market**: use the current last traded price.
- **Market vs Limit**: use the limit order's price.
- **Limit vs Limit**: use the price of the order that arrived first.

### Quantity handling

- Executed quantity is `min(buy_qty, sell_qty)`.
- Any unfilled remainder stays pending for future matches.

## Console Output

After each processed order, the program prints:

- Current last traded price.
- Side-by-side pending buy and sell order tables.

This is useful for observing the order book evolution step by step.

## Output File

After processing all orders, the program writes:

- Executed trade logs.
- Remaining unexecuted order summaries.

The output filename is derived from input by replacing `input...` with `output...` based on the numeric suffix logic in `main.cpp`.

Example:

- Input: `input1.txt`
- Output: `output1.txt`

### Output line format

Executed entries:

```text
order <id> <qty> shares purchased at price <p>
order <id> <qty> shares sold at price <p>
```

Unexecuted entries:

```text
order <id> <qty> shares unexecuted
```

## Notes and Limitations

- Input parsing expects whitespace-separated tokens (not comma-separated CSV).
- Invalid order lines are skipped with an error message.
- The program assumes the input file can be opened and that the first value is a valid numeric price.
- The provided `clean` target in `main/makefile` uses `rm`, which works in Unix-like shells. On Windows PowerShell, delete manually if needed.

## Future Improvements

- Better input-file validation and error handling.
- Configurable output filename.
- Unit tests for matching and pricing rules.
- Cross-platform makefile clean target behavior.
- Split matching logic into reusable classes/modules.
