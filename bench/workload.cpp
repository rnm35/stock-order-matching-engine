#include "workload.hpp"

namespace ome::bench {
namespace {

class Generator {
public:
    explicit Generator(std::uint64_t seed) : state_(seed) {}

    void prefill(std::size_t orders, std::vector<Request>& out) {
        while (shadow_.orderCount() < orders) issue(passive(), out);
    }

    void measured(const WorkloadConfig& c, std::vector<Request>& out) {
        const std::size_t low = c.restingOrders - c.restingOrders / 10;
        const std::size_t high = c.restingOrders + c.restingOrders / 10;
        for (std::size_t i = 0; i < c.ops; ++i) {
            if (below(100) < 2) mid_ += below(2) == 0 ? -1 : 1;  // slow random walk
            issue(next(c, low, high), out);
        }
    }

private:
    // Holds the depth within [low, high] so the book size being measured stays the one
    // configured: below it, add; above it, cancel; otherwise follow the configured mix.
    // Anything that can't be done (no live order to cancel/modify) becomes a passive add.
    Request next(const WorkloadConfig& c, std::size_t low, std::size_t high) {
        const std::size_t depth = shadow_.orderCount();
        const auto roll = below(100);
        Request r;
        if (depth >= low) {
            if (depth > high || roll < c.cancelPct) {
                if (cancel(r)) return r;
            } else if (roll < c.cancelPct + c.limitPct) {
                return below(10) < 7 ? passive() : aggressive();
            } else if (roll < c.cancelPct + c.limitPct + c.marketPct) {
                return market();
            } else if (modify(r)) {
                return r;
            }
        }
        return passive();
    }

    std::uint64_t random() {  // splitmix64
        std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31U);
    }
    std::uint64_t below(std::uint64_t n) { return random() % n; }
    Side side() { return below(2) == 0 ? Side::Buy : Side::Sell; }
    Quantity quantity(std::uint64_t max) { return 1 + static_cast<Quantity>(below(max)); }

    void issue(const Request& r, std::vector<Request>& out) {
        apply(shadow_, r);
        out.push_back(r);
        if (r.kind == Request::Kind::Submit) live_.push_back(r.order.id);
    }

    // Joins the book 1-50 ticks behind the mid: never trades on arrival.
    Request passive() {
        Request r;
        const Side s = side();
        const Price offset = 1 + static_cast<Price>(below(50));
        r.order = NewOrder::limit(nextId_++, s, quantity(100),
                                  s == Side::Buy ? mid_ - offset : mid_ + offset);
        return r;
    }

    // Priced up to 2 ticks through the mid, so it usually trades and may rest the remainder.
    Request aggressive() {
        Request r;
        const Side s = side();
        const auto offset = static_cast<Price>(below(3));
        r.order = NewOrder::limit(nextId_++, s, quantity(100),
                                  s == Side::Buy ? mid_ + offset : mid_ - offset);
        return r;
    }

    Request market() {
        Request r;
        r.order = NewOrder::market(nextId_++, side(), quantity(200));
        return r;
    }

    // Picks a random resting order; forgets ids that have since filled or been cancelled.
    bool pickLive(OrderId& id) {
        for (int attempt = 0; attempt < 16 && !live_.empty(); ++attempt) {
            const std::size_t i = below(live_.size());
            id = live_[i];
            if (shadow_.contains(id)) return true;
            live_[i] = live_.back();
            live_.pop_back();
        }
        return false;
    }

    bool cancel(Request& r) {
        if (!pickLive(r.id)) return false;
        r.kind = Request::Kind::Cancel;
        return true;
    }

    bool modify(Request& r) {
        if (!pickLive(r.id)) return false;
        const auto current = shadow_.find(r.id);
        if (!current) return false;  // can't happen: pickLive checked it is resting
        r.kind = Request::Kind::Modify;
        r.price = current->price;
        r.quantity = current->remaining;
        const auto how = below(10);
        if (how < 6) {  // reduce size: keeps priority
            r.quantity = quantity(static_cast<std::uint64_t>(current->remaining));
        } else if (how < 8) {  // increase size: loses priority
            r.quantity += quantity(50);
        } else {  // re-price by 1-3 ticks either way
            const Price move = 1 + static_cast<Price>(below(3));
            r.price += below(2) == 0 ? move : -move;
        }
        return true;
    }

    std::uint64_t state_;
    EventListener noEvents_;
    OrderBook shadow_{{MatchingMode::Modern, std::nullopt}, noEvents_};
    std::vector<OrderId> live_;
    OrderId nextId_ = 1;
    Price mid_ = 10'000;
};

}  // namespace

Workload makeWorkload(const WorkloadConfig& config) {
    Workload w;
    Generator gen(config.seed);
    w.prefill.reserve(config.restingOrders);
    w.ops.reserve(config.ops);
    gen.prefill(config.restingOrders, w.prefill);
    gen.measured(config, w.ops);
    return w;
}

}  // namespace ome::bench
