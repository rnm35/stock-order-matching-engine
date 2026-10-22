#pragma once

#include "ome/types.hpp"

namespace ome {

// A request to add an order. `price` is ignored for market orders.
struct NewOrder {
    OrderId id = 0;
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    Quantity quantity = 0;
    Price price = 0;
    TimeInForce tif = TimeInForce::GTC;

    static NewOrder limit(OrderId id, Side side, Quantity qty, Price price,
                          TimeInForce tif = TimeInForce::GTC) {
        return {id, side, OrderType::Limit, qty, price, tif};
    }
    static NewOrder market(OrderId id, Side side, Quantity qty,
                           TimeInForce tif = TimeInForce::GTC) {
        return {id, side, OrderType::Market, qty, 0, tif};
    }
};

// An order resting in the book. Only GTC orders ever rest.
struct Order {
    OrderId id = 0;
    Side side = Side::Buy;
    OrderType type = OrderType::Limit;
    Price price = 0;         // meaningless for (legacy) resting market orders
    Quantity remaining = 0;  // open quantity still available to trade
    Sequence sequence = 0;   // time priority: lower = earlier

    friend bool operator==(const Order&, const Order&) = default;
};

}  // namespace ome
