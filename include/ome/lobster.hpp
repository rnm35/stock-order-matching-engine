#pragma once

#include "ome/events.hpp"
#include "ome/order_book.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

// Adapter for LOBSTER message files (reconstructed Nasdaq order flow, lobsterdata.com).
//
// Each CSV line: time (seconds after midnight), event type, order id, size, price (dollars
// x 10000), direction (1 = buy order, -1 = sell order). Event types:
//   1 new limit order   2 partial cancel   3 full delete   4 execution of a visible order
//   5 execution of a hidden order   6 cross trade (auction)   7 trading halt
//
// An incoming order that trades on arrival never appears as a type-1 message; only the
// executions of the resting orders it hit do (type 4). So the replayer turns each type-4
// message into an IOC order on the other side at that price, and checks that OUR book
// fills exactly the resting order LOBSTER says was filled, for the same size. That makes
// real Nasdaq data a test of price-time priority.
//
// A message file only contains events inside the requested number of price levels (for a
// "level 1" file: the best bid and ask). An order that is cancelled while outside that
// range leaves no message, so it would stay in our book forever and distort everything
// after it. reconcile() uses the matching orderbook file (the true best bid/ask after every
// message) to measure agreement and to remove such orders.
namespace ome::lobster {

enum class EventType : std::uint8_t {
    Submit = 1,
    PartialCancel = 2,
    Delete = 3,
    ExecuteVisible = 4,
    ExecuteHidden = 5,
    Cross = 6,
    Halt = 7,
};

struct Message {
    double time = 0;  // seconds after midnight; informational only
    EventType type = EventType::Submit;
    std::uint64_t orderId = 0;
    Quantity size = 0;
    std::int64_t price = 0;  // dollars x 10000, as in the file
    Side side = Side::Buy;   // side of the order the message is about
};

// Parses one message line; nullopt if it isn't a well-formed message.
std::optional<Message> parseMessage(std::string_view line);

// The true best ask and bid after a message: the first four columns of the matching
// orderbook-file line (ask price, ask size, bid price, bid size; prices dollars x 10000).
// LOBSTER writes an empty side as a dummy price with size 0; that is read as no price.
struct TopOfBook {
    std::optional<std::int64_t> askPrice;
    Quantity askSize = 0;
    std::optional<std::int64_t> bidPrice;
    Quantity bidSize = 0;
};

std::optional<TopOfBook> parseOrderbookRow(std::string_view line);

struct ReplayStats {
    std::uint64_t messages = 0;
    std::uint64_t submits = 0;
    std::uint64_t partialCancels = 0;
    std::uint64_t deletes = 0;
    std::uint64_t executions = 0;
    std::uint64_t ignored = 0;  // hidden executions, cross trades, halts
    // Cancels/executions of an order we never saw (e.g. placed before the file starts).
    std::uint64_t unknownOrder = 0;
    std::uint64_t subTickPrice = 0;          // price not a whole number of cents; skipped
    std::uint64_t executionsMatched = 0;     // our book filled the same order for the same size
    std::uint64_t executionsMismatched = 0;  // our book filled something else
    std::uint64_t submitsThatTraded = 0;     // a new order crossed our book (books have diverged)
    // Filled by reconcile() only.
    std::uint64_t topOfBookChecked = 0;
    std::uint64_t topOfBookMatched = 0;    // our best bid and ask, price and size, were right
    std::uint64_t staleOrdersRemoved = 0;  // priced better than the true best: provably gone
};

class Replayer : private EventListener {
public:
    Replayer();

    void apply(const Message& message);

    // Call after apply() with the orderbook line for the same message. Counts whether our
    // best bid and ask (price and size) match, then cancels any of our orders priced better
    // than the true best on their side: Nasdaq can't have them (they would be the best), so
    // their removal must have been filtered out of the message file. Stale orders AT the best
    // price can't be told apart from live ones, so those are left alone.
    void reconcile(const TopOfBook& truth);

    const ReplayStats& stats() const noexcept { return stats_; }
    const OrderBook& book() const noexcept { return book_; }

private:
    void onTrade(const Trade& trade) override { trades_.push_back(trade); }
    std::optional<Price> toTicks(std::int64_t lobsterPrice);
    void execute(const Message& message, Price price);
    bool sideMatches(Side side, std::optional<std::int64_t> price, Quantity size) const;
    void removeBetterThan(Side side, std::optional<std::int64_t> truePrice);

    OrderBook book_;
    ReplayStats stats_;
    std::vector<Trade> trades_;  // trades from the current message
    // Synthetic ids for the aggressor orders we create, far above any LOBSTER order id.
    OrderId nextAggressorId_ = OrderId{1} << 62U;
};

}  // namespace ome::lobster
