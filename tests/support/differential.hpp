#pragma once

#include "ome/order_book.hpp"
#include "reference_book.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace ome::test {

// One request to a book.
struct Op {
    enum class Kind : std::uint8_t { Submit, Cancel, Modify };
    Kind kind = Kind::Submit;
    NewOrder order;         // Submit
    OrderId id = 0;         // Cancel, Modify
    Price price = 0;        // Modify
    Quantity quantity = 0;  // Modify

    std::string describe() const;
};

// Records every event as one line of text, so two books' outputs can be compared and
// printed when they differ.
class EventLog : public EventListener {
public:
    std::vector<std::string> lines;

    void onAccepted(const OrderAccepted& e) override;
    void onTrade(const Trade& t) override;
    void onCancelled(const OrderCancelled& e) override;
    void onModified(const OrderModified& e) override;
    void onRejected(const OrderRejected& e) override;
};

// Runs the same operations through OrderBook and ReferenceBook.
class DifferentialRunner {
public:
    explicit DifferentialRunner(BookConfig config);

    // Applies `op` to both books. Returns "" if both emitted identical events, otherwise a
    // description of the first difference.
    std::string apply(const Op& op);

    // Compares the two books' full state and checks OrderBook's internal invariants
    // (cached level totals, no crossed book, ...). Returns "" if everything holds.
    std::string checkState() const;

    const OrderBook& book() const { return engine_; }
    // Events from the last apply() (identical in both books when apply() returned "").
    const std::vector<std::string>& lastEvents() const { return engineLog_.lines; }

private:
    EventLog engineLog_;
    EventLog referenceLog_;
    OrderBook engine_;
    ReferenceBook reference_;
};

// Produces a random but reproducible stream of operations: prices clustered around a
// drifting mid so that crossing and same-price ties are common, a mix of order types and
// time-in-force, cancels/modifies of live and dead ids, and some invalid requests.
// It peeks at `book` to pick ids that are likely to be live; any book is fine as long as
// both books under test receive the same stream.
class OpGenerator {
public:
    OpGenerator(std::uint64_t seed, const OrderBook& book) : state_(seed), book_(book) {}
    Op next();

private:
    std::uint64_t random();  // splitmix64: same sequence on every compiler/platform
    std::uint64_t below(std::uint64_t n) { return random() % n; }
    bool chance(std::uint64_t percent) { return below(100) < percent; }
    OrderId pickExistingId();
    Op newOrder(OrderType type);

    std::uint64_t state_;
    const OrderBook& book_;
    std::vector<OrderId> candidates_;  // ids that may still be resting
    OrderId nextId_ = 1;
    Price mid_ = 10'000;
    std::uint64_t count_ = 0;
    bool thin_ = false;
};

// Turns arbitrary bytes into operations (for coverage-guided fuzzing). Uses a small id
// space so that duplicates, cancels and modifies frequently hit real orders.
std::vector<Op> decodeOps(const std::uint8_t* data, std::size_t size);

}  // namespace ome::test
