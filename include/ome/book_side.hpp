#pragma once

#include "ome/order.hpp"

#include <list>
#include <map>
#include <memory_resource>
#include <utility>

namespace ome {

// All resting orders at one price, oldest first. Orders are only ever appended at the back
// and traded from the front, so list order == sequence order == time priority.
//
// Allocator-aware (allocator_type + allocator-taking constructors) so that when a level is
// created inside a std::pmr::map, its order list allocates from the same memory pool.
struct PriceLevel {
    using allocator_type = std::pmr::polymorphic_allocator<Order>;

    PriceLevel() = default;
    explicit PriceLevel(const allocator_type& alloc) : orders(alloc) {}
    PriceLevel(const PriceLevel& other, const allocator_type& alloc)
        : totalQuantity(other.totalQuantity), orders(other.orders, alloc) {}
    PriceLevel(PriceLevel&& other, const allocator_type& alloc)
        : totalQuantity(other.totalQuantity), orders(std::move(other.orders), alloc) {}

    // Cached sum of `remaining` over `orders`, so FOK checks and depth queries don't have
    // to walk the queue.
    Quantity totalQuantity = 0;
    // A list because iterators stay valid when other orders are inserted or erased: the
    // order index can hold an iterator and cancel in O(1).
    std::pmr::list<Order> orders;
};

// One side of the book (all bids or all asks).
class BookSide {
public:
    // Ordering of price levels: bids descending, asks ascending, so begin() is always the
    // best price on either side. The direction is a runtime flag rather than a template
    // parameter so both sides have the same map type; that lets the order index store a
    // map iterator for either side and erase an emptied level in O(1).
    struct BestPriceFirst {
        bool descending;
        bool operator()(Price a, Price b) const noexcept { return descending ? a > b : a < b; }
    };
    using Levels = std::pmr::map<Price, PriceLevel, BestPriceFirst>;

    // All nodes (levels and orders) are allocated from `memory`, which must outlive this.
    BookSide(Side side, std::pmr::memory_resource* memory)
        : side_(side), levels_(BestPriceFirst{side == Side::Buy}, memory), marketQueue_(memory) {}

    Side side() const noexcept { return side_; }

    Levels& levels() noexcept { return levels_; }
    const Levels& levels() const noexcept { return levels_; }

    // Returns the level at `price`, creating an empty one if needed. O(log L).
    Levels::iterator levelAt(Price price) { return levels_.try_emplace(price).first; }

    // Legacy mode only: resting market orders. They have no price, and the original rules
    // match them before any limit order on the same side, so they get their own FIFO
    // instead of a fake "infinitely good" price level.
    PriceLevel& marketQueue() noexcept { return marketQueue_; }
    const PriceLevel& marketQueue() const noexcept { return marketQueue_; }

private:
    Side side_;
    Levels levels_;
    PriceLevel marketQueue_;
};

}  // namespace ome
