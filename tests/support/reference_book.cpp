#include "reference_book.hpp"

#include <algorithm>
#include <stdexcept>

namespace ome::test {

ReferenceBook::ReferenceBook(BookConfig config, EventListener& listener)
    : mode_(config.mode), listener_(listener), lastTradePrice_(config.lastTradePrice) {
    if (mode_ == MatchingMode::Legacy && (!lastTradePrice_ || *lastTradePrice_ <= 0)) {
        throw std::invalid_argument("legacy mode needs a positive initial last traded price");
    }
}

bool ReferenceBook::hasPriority(const Order& a, const Order& b) {
    // 1. Legacy resting market orders before any limit order.
    const bool aMarket = a.type == OrderType::Market;
    const bool bMarket = b.type == OrderType::Market;
    if (aMarket != bMarket) return aMarket;
    // 2. Better price (higher bid, lower ask).
    if (!aMarket && a.price != b.price) {
        return a.side == Side::Buy ? a.price > b.price : a.price < b.price;
    }
    // 3. Earlier arrival.
    return a.sequence < b.sequence;
}

bool ReferenceBook::acceptable(const Order& incoming, const Order& resting) {
    if (resting.side == incoming.side) return false;
    if (incoming.type == OrderType::Market || resting.type == OrderType::Market) return true;
    return incoming.side == Side::Buy ? resting.price <= incoming.price
                                      : resting.price >= incoming.price;
}

Price ReferenceBook::tradePrice(const Order& incoming, const Order& resting) const {
    if (resting.type == OrderType::Limit) return resting.price;  // limit vs limit, market vs limit
    if (incoming.type == OrderType::Limit) return incoming.price;  // limit vs market (legacy)
    return *lastTradePrice_;  // NOLINT(bugprone-unchecked-optional-access): always set in legacy
}

std::size_t ReferenceBook::bestMatch(const Order& incoming) const {
    std::size_t best = kNone;
    for (std::size_t i = 0; i < resting_.size(); ++i) {
        if (!acceptable(incoming, resting_[i])) continue;
        if (best == kNone || hasPriority(resting_[i], resting_[best])) best = i;
    }
    return best;
}

std::size_t ReferenceBook::indexOf(OrderId id) const {
    for (std::size_t i = 0; i < resting_.size(); ++i) {
        if (resting_[i].id == id) return i;
    }
    return kNone;
}

void ReferenceBook::eraseAt(std::size_t i) {
    resting_.erase(resting_.begin() + static_cast<std::ptrdiff_t>(i));
}

void ReferenceBook::match(Order& incoming) {
    while (incoming.remaining > 0) {
        const std::size_t i = bestMatch(incoming);
        if (i == kNone) return;
        Order& resting = resting_[i];

        const Quantity qty = std::min(incoming.remaining, resting.remaining);
        const Price price = tradePrice(incoming, resting);
        const bool buy = incoming.side == Side::Buy;
        const Trade trade{buy ? incoming.id : resting.id, buy ? resting.id : incoming.id,
                          incoming.side, price, qty};

        incoming.remaining -= qty;
        resting.remaining -= qty;
        lastTradePrice_ = price;
        if (resting.remaining == 0) eraseAt(i);
        listener_.onTrade(trade);
    }
}

void ReferenceBook::submit(const NewOrder& req) {
    if (req.quantity <= 0)
        return listener_.onRejected({req.id, RequestType::New, RejectReason::InvalidQuantity});
    if (req.type == OrderType::Limit && req.price <= 0) {
        return listener_.onRejected({req.id, RequestType::New, RejectReason::InvalidPrice});
    }
    if (indexOf(req.id) != kNone) {
        return listener_.onRejected({req.id, RequestType::New, RejectReason::DuplicateOrderId});
    }

    const Price price = req.type == OrderType::Limit ? req.price : 0;
    Order order{req.id, req.side, req.type, price, req.quantity, nextSequence_++};
    listener_.onAccepted(
        {order.id, order.side, order.type, req.tif, order.price, order.remaining, order.sequence});

    if (req.tif == TimeInForce::FOK) {
        Quantity available = 0;
        for (const Order& r : resting_) {
            if (acceptable(order, r)) available += r.remaining;
        }
        if (available < order.remaining) {
            return listener_.onCancelled({order.id, order.remaining, CancelReason::FillOrKill});
        }
    }

    match(order);
    if (order.remaining == 0) return;

    const bool rests = req.tif == TimeInForce::GTC &&
                       (order.type == OrderType::Limit || mode_ == MatchingMode::Legacy);
    if (rests) {
        resting_.push_back(order);
    } else {
        listener_.onCancelled({order.id, order.remaining, CancelReason::ImmediateRemainder});
    }
}

void ReferenceBook::cancel(OrderId id) {
    const std::size_t i = indexOf(id);
    if (i == kNone)
        return listener_.onRejected({id, RequestType::Cancel, RejectReason::UnknownOrder});
    const Quantity open = resting_[i].remaining;
    eraseAt(i);
    listener_.onCancelled({id, open, CancelReason::UserRequest});
}

void ReferenceBook::modify(OrderId id, Price newPrice, Quantity newQuantity) {
    const std::size_t i = indexOf(id);
    if (i == kNone)
        return listener_.onRejected({id, RequestType::Modify, RejectReason::UnknownOrder});
    Order& order = resting_[i];
    if (order.type == OrderType::Market) {
        return listener_.onRejected(
            {id, RequestType::Modify, RejectReason::MarketOrderNotModifiable});
    }
    if (newQuantity <= 0)
        return listener_.onRejected({id, RequestType::Modify, RejectReason::InvalidQuantity});
    if (newPrice <= 0)
        return listener_.onRejected({id, RequestType::Modify, RejectReason::InvalidPrice});

    if (newPrice == order.price && newQuantity <= order.remaining) {
        order.remaining = newQuantity;  // same sequence: keeps priority
        return listener_.onModified({id, newPrice, newQuantity, true, order.sequence});
    }

    Order replacement = order;
    replacement.price = newPrice;
    replacement.remaining = newQuantity;
    replacement.sequence = nextSequence_++;
    eraseAt(i);
    listener_.onModified({id, newPrice, newQuantity, false, replacement.sequence});

    match(replacement);
    if (replacement.remaining > 0) resting_.push_back(replacement);
}

std::vector<Order> ReferenceBook::orders(Side side) const {
    std::vector<Order> out;
    for (const Order& o : resting_) {
        if (o.side == side) out.push_back(o);
    }
    std::ranges::sort(out, hasPriority);
    return out;
}

}  // namespace ome::test
