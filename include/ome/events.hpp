#pragma once

#include "ome/types.hpp"

#include <string_view>

namespace ome {

// The book reports everything that happens through these events instead of printing or
// building strings. The CLI, tests and (later) the web UI each decide what to do with them.
//
// For every valid new order the sequence is:
//   Accepted, then zero or more Trades, then at most one Cancelled (unfilled IOC/FOK/market
//   remainder). Whatever is left after that is resting in the book.

struct OrderAccepted {
    OrderId id;
    Side side;
    OrderType type;
    TimeInForce tif;
    Price price;  // 0 for market orders
    Quantity quantity;
    Sequence sequence;
};

struct Trade {
    OrderId buyOrderId;
    OrderId sellOrderId;
    Side aggressorSide;  // side of the incoming order that caused the trade
    Price price;
    Quantity quantity;

    friend bool operator==(const Trade&, const Trade&) = default;
};

enum class CancelReason : std::uint8_t {
    UserRequest,         // cancel() was called
    ImmediateRemainder,  // IOC remainder, or a modern-mode market order with nothing left to hit
    FillOrKill,          // FOK order could not be filled in full, so nothing was traded
};

struct OrderCancelled {
    OrderId id;
    Quantity cancelledQuantity;
    CancelReason reason;
};

struct OrderModified {
    OrderId id;
    Price newPrice;
    Quantity newQuantity;
    bool priorityKept;  // false if the order moved to the back of the queue
    Sequence sequence;  // the order's sequence number after the modify
};

enum class RequestType : std::uint8_t { New, Cancel, Modify };

enum class RejectReason : std::uint8_t {
    DuplicateOrderId,  // an order with this id is already resting
    InvalidQuantity,   // quantity <= 0
    InvalidPrice,      // limit price <= 0
    UnknownOrder,      // cancel/modify of an id that is not resting
    MarketOrderNotModifiable,
};

struct OrderRejected {
    OrderId id;
    RequestType request;
    RejectReason reason;
};

// Default implementations do nothing, so a listener only overrides what it cares about.
class EventListener {
public:
    virtual ~EventListener() = default;
    virtual void onAccepted(const OrderAccepted&) {}
    virtual void onTrade(const Trade&) {}
    virtual void onCancelled(const OrderCancelled&) {}
    virtual void onModified(const OrderModified&) {}
    virtual void onRejected(const OrderRejected&) {}
};

std::string_view toString(CancelReason r) noexcept;
std::string_view toString(RequestType r) noexcept;
std::string_view toString(RejectReason r) noexcept;

}  // namespace ome
