#include "differential.hpp"

#include <algorithm>
#include <sstream>

namespace ome::test {
namespace {

std::string describeOrders(const std::vector<Order>& orders) {
    std::ostringstream os;
    for (const Order& o : orders) {
        os << "  #" << o.id << ' ' << toString(o.side) << ' ' << toString(o.type)
           << " px=" << o.price << " qty=" << o.remaining << " seq=" << o.sequence << '\n';
    }
    return os.str();
}

std::string firstDifference(const std::vector<std::string>& engine,
                            const std::vector<std::string>& reference) {
    std::ostringstream os;
    const std::size_t n = std::max(engine.size(), reference.size());
    for (std::size_t i = 0; i < n; ++i) {
        const std::string e = i < engine.size() ? engine[i] : "(none)";
        const std::string r = i < reference.size() ? reference[i] : "(none)";
        if (e != r) {
            os << "event " << i << " differs\n  engine:    " << e << "\n  reference: " << r << '\n';
            return os.str();
        }
    }
    return {};
}

// 0 -> IOC, 1 -> FOK, anything else -> GTC.
TimeInForce timeInForceFrom(std::uint64_t n) {
    switch (n) {
        case 0: return TimeInForce::IOC;
        case 1: return TimeInForce::FOK;
        default: return TimeInForce::GTC;
    }
}

}  // namespace

// ---------------------------------------------------------------------------------------

std::string Op::describe() const {
    std::ostringstream os;
    switch (kind) {
        case Kind::Submit:
            os << "submit #" << order.id << ' ' << toString(order.side) << ' '
               << toString(order.type) << ' ' << toString(order.tif) << " qty=" << order.quantity;
            if (order.type == OrderType::Limit) os << " px=" << order.price;
            break;
        case Kind::Cancel: os << "cancel #" << id; break;
        case Kind::Modify: os << "modify #" << id << " px=" << price << " qty=" << quantity; break;
    }
    return os.str();
}

void EventLog::onAccepted(const OrderAccepted& e) {
    std::ostringstream os;
    os << "accepted #" << e.id << ' ' << toString(e.side) << ' ' << toString(e.type) << ' '
       << toString(e.tif) << " px=" << e.price << " qty=" << e.quantity << " seq=" << e.sequence;
    lines.push_back(os.str());
}

void EventLog::onTrade(const Trade& t) {
    std::ostringstream os;
    os << "trade buy=#" << t.buyOrderId << " sell=#" << t.sellOrderId
       << " aggressor=" << toString(t.aggressorSide) << " px=" << t.price << " qty=" << t.quantity;
    lines.push_back(os.str());
}

void EventLog::onCancelled(const OrderCancelled& e) {
    std::ostringstream os;
    os << "cancelled #" << e.id << " qty=" << e.cancelledQuantity << " (" << toString(e.reason)
       << ')';
    lines.push_back(os.str());
}

void EventLog::onModified(const OrderModified& e) {
    std::ostringstream os;
    os << "modified #" << e.id << " px=" << e.newPrice << " qty=" << e.newQuantity
       << (e.priorityKept ? " kept" : " lost") << " priority seq=" << e.sequence;
    lines.push_back(os.str());
}

void EventLog::onRejected(const OrderRejected& e) {
    std::ostringstream os;
    os << "rejected #" << e.id << ' ' << toString(e.request) << " (" << toString(e.reason) << ')';
    lines.push_back(os.str());
}

// ---------------------------------------------------------------------------------------

DifferentialRunner::DifferentialRunner(BookConfig config)
    : engine_(config, engineLog_), reference_(config, referenceLog_) {}

std::string DifferentialRunner::apply(const Op& op) {
    engineLog_.lines.clear();
    referenceLog_.lines.clear();
    switch (op.kind) {
        case Op::Kind::Submit:
            engine_.submit(op.order);
            reference_.submit(op.order);
            break;
        case Op::Kind::Cancel:
            engine_.cancel(op.id);
            reference_.cancel(op.id);
            break;
        case Op::Kind::Modify:
            engine_.modify(op.id, op.price, op.quantity);
            reference_.modify(op.id, op.price, op.quantity);
            break;
    }
    return firstDifference(engineLog_.lines, referenceLog_.lines);
}

std::string DifferentialRunner::checkState() const {
    std::ostringstream err;

    if (engine_.lastTradePrice() != reference_.lastTradePrice()) {
        err << "last traded price differs\n";
    }

    std::size_t total = 0;
    for (Side side : {Side::Buy, Side::Sell}) {
        const auto mine = engine_.orders(side);
        const auto theirs = reference_.orders(side);
        if (mine != theirs) {
            err << toString(side) << " side differs\n engine:\n"
                << describeOrders(mine) << " reference:\n"
                << describeOrders(theirs);
        }
        total += mine.size();

        // Every resting order must be findable, and the cached per-level totals must match
        // the orders actually queued there.
        std::vector<LevelSummary> expected;
        for (const Order& o : mine) {
            if (engine_.find(o.id) != o) err << "find(#" << o.id << ") is stale\n";
            if (o.type == OrderType::Market) continue;
            if (expected.empty() || expected.back().price != o.price) {
                expected.push_back({o.price, 0, 0});
            }
            expected.back().quantity += o.remaining;
            ++expected.back().orderCount;
        }
        const auto depth = engine_.depth(side);
        const bool sameDepth =
            std::ranges::equal(depth, expected, [](const LevelSummary& a, const LevelSummary& b) {
                return a.price == b.price && a.quantity == b.quantity &&
                       a.orderCount == b.orderCount;
            });
        if (!sameDepth) err << toString(side) << " depth/level totals are inconsistent\n";
    }
    if (total != engine_.orderCount()) err << "orderCount() disagrees with the book\n";

    // After every operation the book must be uncrossed: anything that could trade has.
    const auto bid = engine_.bestBid();
    const auto ask = engine_.bestAsk();
    if (bid && ask && *bid >= *ask) err << "book is crossed: " << *bid << " >= " << *ask << '\n';
    for (Side side : {Side::Buy, Side::Sell}) {
        const auto orders = engine_.orders(side);
        const bool hasMarket = !orders.empty() && orders.front().type == OrderType::Market;
        if (hasMarket && !engine_.orders(opposite(side)).empty()) {
            err << "resting market order with liquidity on the other side\n";
        }
    }
    return err.str();
}

// ---------------------------------------------------------------------------------------

std::uint64_t OpGenerator::random() {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

OrderId OpGenerator::pickExistingId() {
    // Usually a live order; sometimes one that has gone (filled/cancelled), to test rejects.
    while (!candidates_.empty()) {
        const std::size_t i = below(candidates_.size());
        const OrderId id = candidates_[i];
        if (book_.contains(id)) return id;
        candidates_[i] = candidates_.back();
        candidates_.pop_back();
        if (chance(10)) return id;
    }
    return nextId_;  // never issued
}

Op OpGenerator::newOrder(OrderType type) {
    Op op;
    NewOrder& o = op.order;
    // Mostly fresh ids; occasionally reuse one (duplicate if still resting, legal if not).
    o.id = chance(3) ? pickExistingId() : nextId_++;
    o.side = chance(50) ? Side::Buy : Side::Sell;
    o.type = type;
    // Most orders join the book up to 15 ticks behind the mid (building depth and same-price
    // queues); about a third are priced through it and trade.
    const Price offset =
        chance(30) ? static_cast<Price>(below(6)) : -1 - static_cast<Price>(below(15));
    o.price = o.side == Side::Buy ? mid_ + offset : mid_ - offset;
    if (chance(1)) o.price = chance(50) ? 0 : -5;
    o.quantity = chance(5) ? 100 + static_cast<Quantity>(below(900))  // sweeps several levels
                           : 1 + static_cast<Quantity>(below(100));
    if (chance(1)) o.quantity = 0;
    o.tif = timeInForceFrom(below(10));  // 10% IOC, 10% FOK, 80% GTC
    candidates_.push_back(o.id);
    return op;
}

Op OpGenerator::next() {
    if (chance(5)) mid_ = std::clamp<Price>(mid_ + static_cast<Price>(below(5)) - 2, 9'000, 11'000);

    // Alternate between a deep book (most of the time) and a thin, cancel- and market-heavy
    // phase in which one side often empties, so legacy market orders get to rest and
    // market-vs-market trades happen.
    thin_ = (count_++ / 5'000) % 4 == 3;

    // Keep the book from growing without bound (the reference book is O(n) per operation).
    const bool crowded = book_.orderCount() > 800;
    const auto r = below(100);
    if ((crowded && r < 50) || (thin_ && r < 35)) {
        Op op;
        op.kind = Op::Kind::Cancel;
        op.id = pickExistingId();
        return op;
    }
    if (r < (thin_ ? 50U : 58U)) return newOrder(OrderType::Limit);
    if (r < (thin_ ? 75U : 65U)) return newOrder(OrderType::Market);

    Op op;
    op.id = pickExistingId();
    if (r < 77) {
        op.kind = Op::Kind::Cancel;
        return op;
    }

    op.kind = Op::Kind::Modify;
    const auto current = book_.find(op.id);
    const Price px = current ? current->price : mid_;
    const Quantity qty = current ? current->remaining : 10;
    const auto how = below(10);
    if (how < 4) {  // reduce in place
        op.price = px;
        op.quantity = 1 + static_cast<Quantity>(below(static_cast<std::uint64_t>(qty)));
    } else if (how < 6) {  // increase
        op.price = px;
        op.quantity = qty + 1 + static_cast<Quantity>(below(50));
    } else if (how < 9) {  // move price, possibly across the spread
        op.price = px + static_cast<Price>(below(11)) - 5;
        op.quantity = qty;
    } else {  // invalid
        op.price = chance(50) ? 0 : px;
        op.quantity = op.price == 0 ? qty : 0;
    }
    return op;
}

// ---------------------------------------------------------------------------------------

std::vector<Op> decodeOps(const std::uint8_t* data, std::size_t size) {
    std::vector<Op> ops;
    for (std::size_t i = 0; i + 4 <= size; i += 4) {
        const unsigned b0 = data[i];  // unsigned so the bit tests below stay unsigned
        const auto offset = static_cast<std::int8_t>(data[i + 1]);
        const std::uint8_t b2 = data[i + 2];
        const OrderId id = data[i + 3] % 32;

        const Price price = offset == -128 ? 0 : 10'000 + offset % 16;
        const Quantity qty = b2 >= 250 ? 1'000 : b2 % 50;

        Op op;
        const unsigned kind = b0 % 8;
        if (kind <= 4) {
            op.kind = Op::Kind::Submit;
            op.order.id = id;
            op.order.side = ((b0 >> 3U) & 1U) != 0 ? Side::Sell : Side::Buy;
            op.order.type = kind == 4 ? OrderType::Market : OrderType::Limit;
            op.order.price = price;
            op.order.quantity = qty;
            op.order.tif = timeInForceFrom((b0 >> 4U) % 4U);
        } else if (kind == 5) {
            op.kind = Op::Kind::Cancel;
            op.id = id;
        } else {
            op.kind = Op::Kind::Modify;
            op.id = id;
            op.price = price;
            op.quantity = qty;
        }
        ops.push_back(op);
    }
    return ops;
}

}  // namespace ome::test
