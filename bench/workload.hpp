#pragma once

#include "ome/order_book.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

// Synthetic order flow for the benchmarks. Unlike the differential-test generator, every
// request is valid (no rejects), so the numbers measure the book doing real work.
namespace ome::bench {

struct Request {
    enum class Kind : std::uint8_t { Submit, Cancel, Modify };
    Kind kind = Kind::Submit;
    NewOrder order;         // Submit
    OrderId id = 0;         // Cancel, Modify
    Price price = 0;        // Modify
    Quantity quantity = 0;  // Modify
};

inline void apply(OrderBook& book, const Request& r) {
    switch (r.kind) {
        case Request::Kind::Submit: book.submit(r.order); break;
        case Request::Kind::Cancel: book.cancel(r.id); break;
        case Request::Kind::Modify: book.modify(r.id, r.price, r.quantity); break;
    }
}

struct WorkloadConfig {
    // Book depth built by the prefill, then held within +-10% during the measured phase.
    std::size_t restingOrders = 10'000;
    std::size_t ops = 1'000'000;  // requests in the measured phase
    std::uint64_t seed = 1;
    // Mix of the measured phase, in percent; the remainder are modifies. Cancels dominating
    // adds is typical of real order flow.
    unsigned limitPct = 45;
    unsigned marketPct = 5;
    unsigned cancelPct = 40;
};

struct Workload {
    std::vector<Request> prefill;  // passive limit orders only; not meant to be timed
    std::vector<Request> ops;      // the measured mix
};

// Deterministic for a given config. Builds the stream by running it through a shadow
// OrderBook (modern mode), so cancels and modifies always target orders that are live at
// that point; replaying the same stream into a fresh book reproduces exactly that state.
Workload makeWorkload(const WorkloadConfig& config);

}  // namespace ome::bench
