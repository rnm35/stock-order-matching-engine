#pragma once

#include "ome/book_side.hpp"
#include "ome/events.hpp"
#include "ome/order.hpp"

#include <cstddef>
#include <limits>
#include <memory_resource>
#include <optional>
#include <unordered_map>
#include <vector>

namespace ome {

struct BookConfig {
    MatchingMode mode = MatchingMode::Modern;
    // Reference price for legacy market-vs-market trades. Required in legacy mode, optional
    // in modern mode (where it only seeds lastTradePrice()).
    std::optional<Price> lastTradePrice;
};

struct LevelSummary {
    Price price;
    Quantity quantity;
    std::size_t orderCount;
};

// A single-instrument limit order book with price-time priority matching.
//
// Complexity (L = number of price levels on a side):
//   submit: O(log L) to find/create the level it rests at, plus O(1) per order it trades with
//   cancel: O(1) (hash lookup + list erase; an emptied level is erased by iterator)
//   modify: O(1) for a size reduction, otherwise cancel + submit
//
// All results are reported through the EventListener, synchronously, during the call.
// A listener must not call back into the book from inside a callback.
class OrderBook {
public:
    // Throws std::invalid_argument if legacy mode is requested without a positive
    // lastTradePrice.
    OrderBook(BookConfig config, EventListener& listener);

    // The book holds iterators into itself, so copying or moving it would leave them
    // dangling.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;

    void submit(const NewOrder& req);
    void cancel(OrderId id);

    // Changes a resting limit order's price and open quantity. Reducing quantity at the
    // same price keeps time priority; anything else loses it (and may trade immediately
    // if the new price crosses the spread).
    void modify(OrderId id, Price newPrice, Quantity newQuantity);

    MatchingMode mode() const noexcept { return mode_; }
    std::optional<Price> bestBid() const;
    std::optional<Price> bestAsk() const;
    std::optional<Price> lastTradePrice() const noexcept { return lastTradePrice_; }

    std::size_t orderCount() const noexcept { return index_.size(); }
    bool contains(OrderId id) const { return index_.contains(id); }
    std::optional<Order> find(OrderId id) const;

    // Total open limit quantity at one price (0 if the level does not exist).
    Quantity volumeAt(Side side, Price price) const;

    // The orders queued at one price, front (next to trade) first. Empty if no such level.
    std::vector<Order> ordersAt(Side side, Price price) const;

    // Price levels best first, at most `maxLevels` of them. Excludes legacy market orders.
    std::vector<LevelSummary> depth(
        Side side, std::size_t maxLevels = std::numeric_limits<std::size_t>::max()) const;

    // Every resting order on one side in matching priority order: legacy market orders
    // first, then by price, then by time.
    std::vector<Order> orders(Side side) const;

private:
    // Where a resting order lives, so cancel/modify never search the book.
    struct Locator {
        Side side;
        bool inMarketQueue;
        BookSide::Levels::iterator level;  // unused when inMarketQueue
        std::pmr::list<Order>::iterator order;
    };
    using Index = std::pmr::unordered_map<OrderId, Locator>;

    BookSide& sideOf(Side s) noexcept { return s == Side::Buy ? bids_ : asks_; }
    const BookSide& sideOf(Side s) const noexcept { return s == Side::Buy ? bids_ : asks_; }
    PriceLevel& levelOf(const Locator& loc) noexcept;

    static bool crosses(const Order& incoming, Price restingPrice) noexcept;
    Price executionPrice(const Order& incoming, const Order& resting) const noexcept;
    Quantity fillableQuantity(const Order& incoming) const;

    void match(Order& incoming);
    void fillFromLevel(Order& incoming, PriceLevel& level);
    void rest(const Order& order);
    void removeResting(Index::iterator it);
    void reject(OrderId id, RequestType request, RejectReason reason);

    MatchingMode mode_;
    EventListener& listener_;
    // Every node the book allocates (orders, price levels, index entries) comes from this
    // pool instead of the global heap: freed nodes are reused by later orders, with no
    // locking (a book is single-threaded). It must be declared before the containers that
    // use it so that it is destroyed after them. Memory is returned to the system only when
    // the book is destroyed.
    std::pmr::unsynchronized_pool_resource pool_;
    BookSide bids_{Side::Buy, &pool_};
    BookSide asks_{Side::Sell, &pool_};
    Index index_{&pool_};
    Sequence nextSequence_ = 1;
    std::optional<Price> lastTradePrice_;
};

}  // namespace ome
