#include "ome/order_book.hpp"

#include <algorithm>
#include <stdexcept>

namespace ome {

OrderBook::OrderBook(BookConfig config, EventListener& listener)
    : mode_(config.mode), listener_(listener), lastTradePrice_(config.lastTradePrice) {
    if (mode_ == MatchingMode::Legacy && (!lastTradePrice_ || *lastTradePrice_ <= 0)) {
        throw std::invalid_argument("legacy mode needs a positive initial last traded price");
    }
}

// ---------------------------------------------------------------------------------------
// Commands

void OrderBook::submit(const NewOrder& req) {
    if (req.quantity <= 0) return reject(req.id, RequestType::New, RejectReason::InvalidQuantity);
    if (req.type == OrderType::Limit && req.price <= 0) {
        return reject(req.id, RequestType::New, RejectReason::InvalidPrice);
    }
    // Ids only need to be unique among resting orders; the index is what cancel/modify use.
    if (index_.contains(req.id)) {
        return reject(req.id, RequestType::New, RejectReason::DuplicateOrderId);
    }

    const Price price = req.type == OrderType::Limit ? req.price : 0;
    Order order{req.id, req.side, req.type, price, req.quantity, nextSequence_++};
    listener_.onAccepted(
        {order.id, order.side, order.type, req.tif, order.price, order.remaining, order.sequence});

    // FOK is checked up front so that a partial fill never happens and has to be undone.
    if (req.tif == TimeInForce::FOK && fillableQuantity(order) < order.remaining) {
        listener_.onCancelled({order.id, order.remaining, CancelReason::FillOrKill});
        return;
    }

    match(order);
    if (order.remaining == 0) return;

    // Only GTC orders rest. Market orders rest only under the legacy rules.
    const bool canRest = req.tif == TimeInForce::GTC &&
                         (order.type == OrderType::Limit || mode_ == MatchingMode::Legacy);
    if (canRest) {
        rest(order);
    } else {
        listener_.onCancelled({order.id, order.remaining, CancelReason::ImmediateRemainder});
    }
}

void OrderBook::cancel(OrderId id) {
    auto it = index_.find(id);
    if (it == index_.end()) return reject(id, RequestType::Cancel, RejectReason::UnknownOrder);

    const Quantity open = it->second.order->remaining;
    removeResting(it);
    listener_.onCancelled({id, open, CancelReason::UserRequest});
}

void OrderBook::modify(OrderId id, Price newPrice, Quantity newQuantity) {
    auto it = index_.find(id);
    if (it == index_.end()) return reject(id, RequestType::Modify, RejectReason::UnknownOrder);

    Order& order = *it->second.order;
    if (order.type == OrderType::Market) {
        return reject(id, RequestType::Modify, RejectReason::MarketOrderNotModifiable);
    }
    if (newQuantity <= 0) return reject(id, RequestType::Modify, RejectReason::InvalidQuantity);
    if (newPrice <= 0) return reject(id, RequestType::Modify, RejectReason::InvalidPrice);

    if (newPrice == order.price && newQuantity <= order.remaining) {
        // Shrinking an order in place doesn't hurt anyone queued behind it, so it keeps its
        // place in the queue (this is how exchanges treat size reductions).
        levelOf(it->second).totalQuantity -= order.remaining - newQuantity;
        order.remaining = newQuantity;
        listener_.onModified({id, newPrice, newQuantity, true, order.sequence});
        return;
    }

    // A price change or size increase is a cancel/replace: the order goes to the back of
    // the queue at its new price, and if that price crosses the spread it trades now.
    Order replacement = order;
    replacement.price = newPrice;
    replacement.remaining = newQuantity;
    replacement.sequence = nextSequence_++;
    removeResting(it);
    listener_.onModified({id, newPrice, newQuantity, false, replacement.sequence});

    match(replacement);
    if (replacement.remaining > 0) rest(replacement);  // resting orders are always GTC
}

// ---------------------------------------------------------------------------------------
// Matching

bool OrderBook::crosses(const Order& incoming, Price restingPrice) noexcept {
    if (incoming.type == OrderType::Market) return true;
    return incoming.side == Side::Buy ? restingPrice <= incoming.price
                                      : restingPrice >= incoming.price;
}

Price OrderBook::executionPrice(const Order& incoming, const Order& resting) const noexcept {
    // Normal case, both modes: the resting order set the price, so it trades at that price.
    // Under the legacy rules this covers "limit vs limit = price of the earlier order"
    // (the resting order is always the earlier one) and "market vs limit = limit price".
    if (resting.type == OrderType::Limit) return resting.price;

    // Resting market orders only exist in legacy mode.
    if (incoming.type == OrderType::Limit) return incoming.price;  // limit vs market
    // Market vs market. The constructor guarantees a last traded price in legacy mode, and
    // every trade updates it, so it is always set here.
    return *lastTradePrice_;  // NOLINT(bugprone-unchecked-optional-access)
}

Quantity OrderBook::fillableQuantity(const Order& incoming) const {
    const BookSide& opp = sideOf(opposite(incoming.side));
    Quantity available = opp.marketQueue().totalQuantity;
    for (const auto& [price, level] : opp.levels()) {
        if (available >= incoming.remaining || !crosses(incoming, price)) break;
        available += level.totalQuantity;
    }
    return available;
}

void OrderBook::match(Order& incoming) {
    BookSide& opp = sideOf(opposite(incoming.side));

    while (incoming.remaining > 0) {
        // Legacy resting market orders accept any price, so they always go first.
        if (!opp.marketQueue().orders.empty()) {
            fillFromLevel(incoming, opp.marketQueue());
            continue;
        }

        auto best = opp.levels().begin();
        if (best == opp.levels().end() || !crosses(incoming, best->first)) break;

        fillFromLevel(incoming, best->second);
        if (best->second.orders.empty()) opp.levels().erase(best);
    }
}

// Trades `incoming` against the queue front to back until one of them runs out.
void OrderBook::fillFromLevel(Order& incoming, PriceLevel& level) {
    while (incoming.remaining > 0 && !level.orders.empty()) {
        Order& resting = level.orders.front();
        const Quantity qty = std::min(incoming.remaining, resting.remaining);
        const Price price = executionPrice(incoming, resting);

        const bool incomingIsBuy = incoming.side == Side::Buy;
        const Trade trade{incomingIsBuy ? incoming.id : resting.id,
                          incomingIsBuy ? resting.id : incoming.id, incoming.side, price, qty};

        incoming.remaining -= qty;
        resting.remaining -= qty;
        level.totalQuantity -= qty;
        lastTradePrice_ = price;

        // A partially filled resting order stays at the front: it keeps its priority.
        if (resting.remaining == 0) {
            index_.erase(resting.id);
            level.orders.pop_front();
        }
        listener_.onTrade(trade);
    }
}

// ---------------------------------------------------------------------------------------
// Book maintenance

void OrderBook::rest(const Order& order) {
    BookSide& own = sideOf(order.side);
    Locator loc{order.side, order.type == OrderType::Market, {}, {}};

    PriceLevel* level = nullptr;
    if (loc.inMarketQueue) {
        level = &own.marketQueue();
    } else {
        loc.level = own.levelAt(order.price);
        level = &loc.level->second;
    }

    level->orders.push_back(order);
    level->totalQuantity += order.remaining;
    loc.order = std::prev(level->orders.end());
    index_.emplace(order.id, loc);
}

void OrderBook::removeResting(Index::iterator it) {
    const Locator& loc = it->second;
    PriceLevel& level = levelOf(loc);

    level.totalQuantity -= loc.order->remaining;
    level.orders.erase(loc.order);
    if (!loc.inMarketQueue && level.orders.empty()) sideOf(loc.side).levels().erase(loc.level);
    index_.erase(it);
}

PriceLevel& OrderBook::levelOf(const Locator& loc) noexcept {
    return loc.inMarketQueue ? sideOf(loc.side).marketQueue() : loc.level->second;
}

void OrderBook::reject(OrderId id, RequestType request, RejectReason reason) {
    listener_.onRejected({id, request, reason});
}

// ---------------------------------------------------------------------------------------
// Queries

std::optional<Price> OrderBook::bestBid() const {
    if (bids_.levels().empty()) return std::nullopt;
    return bids_.levels().begin()->first;
}

std::optional<Price> OrderBook::bestAsk() const {
    if (asks_.levels().empty()) return std::nullopt;
    return asks_.levels().begin()->first;
}

std::optional<Order> OrderBook::find(OrderId id) const {
    auto it = index_.find(id);
    if (it == index_.end()) return std::nullopt;
    return *it->second.order;
}

Quantity OrderBook::volumeAt(Side side, Price price) const {
    const auto& levels = sideOf(side).levels();
    auto it = levels.find(price);
    return it == levels.end() ? 0 : it->second.totalQuantity;
}

std::vector<Order> OrderBook::ordersAt(Side side, Price price) const {
    const auto& levels = sideOf(side).levels();
    auto it = levels.find(price);
    if (it == levels.end()) return {};
    return {it->second.orders.begin(), it->second.orders.end()};
}

std::vector<LevelSummary> OrderBook::depth(Side side, std::size_t maxLevels) const {
    std::vector<LevelSummary> out;
    for (const auto& [price, level] : sideOf(side).levels()) {
        if (out.size() == maxLevels) break;
        out.push_back({price, level.totalQuantity, level.orders.size()});
    }
    return out;
}

std::vector<Order> OrderBook::orders(Side side) const {
    const BookSide& s = sideOf(side);
    std::vector<Order> out(s.marketQueue().orders.begin(), s.marketQueue().orders.end());
    for (const auto& [price, level] : s.levels()) {
        out.insert(out.end(), level.orders.begin(), level.orders.end());
    }
    return out;
}

}  // namespace ome
