#pragma once

#include "ome/events.hpp"
#include "ome/order.hpp"
#include "ome/order_book.hpp"  // BookConfig only; no code is shared with OrderBook

#include <cstddef>
#include <optional>
#include <vector>

namespace ome::test {

// A deliberately naive order book used only as a test oracle.
//
// All resting orders live in one unsorted vector and every operation is a linear scan, so
// each rule is a few obvious lines that can be checked by eye against README "Matching
// Behavior". It is O(n) per operation and must never be used for anything but testing.
// The differential tests feed the same operations to this and to OrderBook and require
// identical events and identical final books.
class ReferenceBook {
public:
    ReferenceBook(BookConfig config, EventListener& listener);

    void submit(const NewOrder& req);
    void cancel(OrderId id);
    void modify(OrderId id, Price newPrice, Quantity newQuantity);

    // Resting orders on one side in priority order (same contract as OrderBook::orders).
    std::vector<Order> orders(Side side) const;
    std::optional<Price> lastTradePrice() const { return lastTradePrice_; }

private:
    // True if resting order `a` must trade before resting order `b` (same side).
    static bool hasPriority(const Order& a, const Order& b);
    static bool acceptable(const Order& incoming, const Order& resting);
    Price tradePrice(const Order& incoming, const Order& resting) const;
    static constexpr std::size_t kNone = static_cast<std::size_t>(-1);
    std::size_t bestMatch(const Order& incoming) const;  // index into resting_, or kNone
    std::size_t indexOf(OrderId id) const;               // index into resting_, or kNone
    void eraseAt(std::size_t i);
    void match(Order& incoming);

    MatchingMode mode_;
    EventListener& listener_;
    std::vector<Order> resting_;
    Sequence nextSequence_ = 1;
    std::optional<Price> lastTradePrice_;
};

}  // namespace ome::test
