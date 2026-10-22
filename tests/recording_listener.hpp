#pragma once

#include "ome/events.hpp"

#include <ostream>
#include <vector>

// Captures every event so tests can assert on exactly what the book reported.
struct RecordingListener : ome::EventListener {
    std::vector<ome::OrderAccepted> accepted;
    std::vector<ome::Trade> trades;
    std::vector<ome::OrderCancelled> cancelled;
    std::vector<ome::OrderModified> modified;
    std::vector<ome::OrderRejected> rejected;

    void onAccepted(const ome::OrderAccepted& e) override { accepted.push_back(e); }
    void onTrade(const ome::Trade& e) override { trades.push_back(e); }
    void onCancelled(const ome::OrderCancelled& e) override { cancelled.push_back(e); }
    void onModified(const ome::OrderModified& e) override { modified.push_back(e); }
    void onRejected(const ome::OrderRejected& e) override { rejected.push_back(e); }
};

namespace ome {
// Readable failure messages in gtest.
inline void PrintTo(const Trade& t, std::ostream* os) {
    *os << "Trade{buy=" << t.buyOrderId << " sell=" << t.sellOrderId
        << " aggressor=" << toString(t.aggressorSide) << " px=" << t.price << " qty=" << t.quantity
        << "}";
}
}  // namespace ome
