// Types for the raw module built from wasm/bindings.cpp. CMake copies this file next to the
// generated ome.js (web/src/engine/wasm/). Application code uses the typed wrapper in
// web/src/engine/engine.ts, which decodes the numeric arrays described here.
//
// Prices are integer ticks (1 tick = 0.01); ids and quantities are whole numbers below 2^53.
// Codes: side 0 = buy, 1 = sell; tif 0 = GTC, 1 = IOC, 2 = FOK; orderType 0 = limit, 1 = market.

export interface RawBook {
  submitLimit(id: number, side: number, quantity: number, price: number, tif: number): void;
  submitMarket(id: number, side: number, quantity: number, tif: number): void;
  cancel(id: number): void;
  modify(id: number, price: number, quantity: number): void;
  /**
   * Events from the last call, 8 numbers each; field 0 is the type:
   *   0 accepted  [0, id, side, orderType, price, quantity, sequence, 0]
   *   1 trade     [1, buyId, sellId, aggressorSide, price, quantity, 0, 0]
   *   2 cancelled [2, id, quantity, reason, 0, 0, 0, 0]
   *   3 modified  [3, id, price, quantity, priorityKept (0/1), sequence, 0, 0]
   *   4 rejected  [4, id, request, reason, 0, 0, 0, 0]
   * A view into wasm memory: read it before calling into the module again.
   */
  events(): Float64Array;
  /** NaN when the side is empty / nothing has traded. */
  bestBid(): number;
  bestAsk(): number;
  lastTradePrice(): number;
  orderCount(): number;
  /** [price, quantity, orders] per level, best first (a view; read before the next call). */
  depth(side: number, maxLevels: number): Float64Array;
  /** [side, orderType, price, remaining], or empty if the order isn't resting. */
  find(id: number): Float64Array;
  /** Frees the C++ object; it must not be used afterwards. */
  delete(): void;
}

export interface OmeModule {
  Book: new (mode: "modern" | "legacy", lastTradePrice: number) => RawBook;
  /** [exception type, message] of a C++ exception caught in JS. */
  getExceptionMessage(exception: unknown): [string, string];
  decrementExceptionRefcount(exception: unknown): void;
}

export default function createOme(options?: object): Promise<OmeModule>;
