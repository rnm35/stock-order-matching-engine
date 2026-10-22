#include "ome/lobster.hpp"

#include <array>
#include <charconv>
#include <cstdlib>
#include <string>

namespace ome::lobster {
namespace {

// LOBSTER prices are dollars x 10000; our tick is one cent.
constexpr std::int64_t kLobsterUnitsPerTick = 10'000 / kTicksPerUnit;

template <typename T>
bool parseInteger(std::string_view text, T& out) {
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && end == text.data() + text.size();
}

// A LOBSTER price as ticks, if it is a positive whole number of cents.
std::optional<Price> exactTicks(std::int64_t lobsterPrice) {
    if (lobsterPrice <= 0 || lobsterPrice % kLobsterUnitsPerTick != 0) return std::nullopt;
    return lobsterPrice / kLobsterUnitsPerTick;
}

// Splits off the first `fields.size()` comma-separated fields; false if there are fewer.
template <std::size_t N>
bool splitFields(std::string_view line, std::array<std::string_view, N>& fields) {
    while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.remove_suffix(1);
    for (auto& field : fields) {
        if (line.data() == nullptr) return false;
        const auto comma = line.find(',');
        field = line.substr(0, comma);
        line = comma == std::string_view::npos ? std::string_view{} : line.substr(comma + 1);
    }
    return true;
}

}  // namespace

std::optional<Message> parseMessage(std::string_view line) {
    // The first six comma-separated fields; any further columns are ignored.
    std::array<std::string_view, 6> fields;
    if (!splitFields(line, fields)) return std::nullopt;

    Message m;
    const std::string time(fields[0]);
    char* end = nullptr;
    m.time = std::strtod(time.c_str(), &end);
    if (time.empty() || end != time.c_str() + time.size()) return std::nullopt;

    int type = 0;
    int direction = 0;
    if (!parseInteger(fields[1], type) || type < 1 || type > 7) return std::nullopt;
    if (!parseInteger(fields[2], m.orderId) || !parseInteger(fields[3], m.size) ||
        !parseInteger(fields[4], m.price) || !parseInteger(fields[5], direction)) {
        return std::nullopt;
    }
    if (direction != 1 && direction != -1) return std::nullopt;
    m.type = static_cast<EventType>(type);
    m.side = direction == 1 ? Side::Buy : Side::Sell;
    return m;
}

std::optional<TopOfBook> parseOrderbookRow(std::string_view line) {
    std::array<std::string_view, 4> fields;
    if (!splitFields(line, fields)) return std::nullopt;
    std::int64_t askPrice = 0;
    std::int64_t bidPrice = 0;
    TopOfBook top;
    if (!parseInteger(fields[0], askPrice) || !parseInteger(fields[1], top.askSize) ||
        !parseInteger(fields[2], bidPrice) || !parseInteger(fields[3], top.bidSize)) {
        return std::nullopt;
    }
    if (top.askSize > 0) top.askPrice = askPrice;  // size 0 = empty side (dummy price)
    if (top.bidSize > 0) top.bidPrice = bidPrice;
    return top;
}

Replayer::Replayer() : book_({MatchingMode::Modern, std::nullopt}, *this) {}

std::optional<Price> Replayer::toTicks(std::int64_t lobsterPrice) {
    const auto ticks = exactTicks(lobsterPrice);
    if (!ticks) ++stats_.subTickPrice;
    return ticks;
}

bool Replayer::sideMatches(Side side, std::optional<std::int64_t> price, Quantity size) const {
    const auto ours = side == Side::Buy ? book_.bestBid() : book_.bestAsk();
    if (!price) return !ours;
    const auto truth = exactTicks(*price);
    return ours && truth && *ours == *truth && book_.volumeAt(side, *ours) == size;
}

void Replayer::reconcile(const TopOfBook& truth) {
    ++stats_.topOfBookChecked;
    if (sideMatches(Side::Sell, truth.askPrice, truth.askSize) &&
        sideMatches(Side::Buy, truth.bidPrice, truth.bidSize)) {
        ++stats_.topOfBookMatched;
    }
    removeBetterThan(Side::Sell, truth.askPrice);
    removeBetterThan(Side::Buy, truth.bidPrice);
}

void Replayer::removeBetterThan(Side side, std::optional<std::int64_t> truePrice) {
    std::optional<Price> limit;  // no limit: the side is really empty, so everything goes
    if (truePrice) {
        limit = exactTicks(*truePrice);
        if (!limit) return;  // can't compare against a sub-cent price; leave the book alone
    }
    for (;;) {
        const auto best = side == Side::Buy ? book_.bestBid() : book_.bestAsk();
        if (!best) return;
        const bool better = !limit || (side == Side::Buy ? *best > *limit : *best < *limit);
        if (!better) return;
        for (const Order& stale : book_.ordersAt(side, *best)) {
            book_.cancel(stale.id);
            ++stats_.staleOrdersRemoved;
        }
    }
}

void Replayer::apply(const Message& m) {
    ++stats_.messages;
    trades_.clear();

    switch (m.type) {
        case EventType::Submit: {
            ++stats_.submits;
            const auto price = toTicks(m.price);
            if (!price) return;
            book_.submit(NewOrder::limit(m.orderId, m.side, m.size, *price));
            if (!trades_.empty()) ++stats_.submitsThatTraded;
            return;
        }
        case EventType::PartialCancel: {
            ++stats_.partialCancels;
            const auto order = book_.find(m.orderId);
            if (!order) {
                ++stats_.unknownOrder;
                return;
            }
            // A size reduction at the same price: keeps the order's queue position.
            const Quantity left = order->remaining - m.size;
            if (left > 0) {
                book_.modify(m.orderId, order->price, left);
            } else {
                book_.cancel(m.orderId);
            }
            return;
        }
        case EventType::Delete:
            ++stats_.deletes;
            if (!book_.contains(m.orderId)) {
                ++stats_.unknownOrder;
                return;
            }
            book_.cancel(m.orderId);
            return;
        case EventType::ExecuteVisible: {
            ++stats_.executions;
            if (!book_.contains(m.orderId)) {
                ++stats_.unknownOrder;
                return;
            }
            if (const auto price = toTicks(m.price)) execute(m, *price);
            return;
        }
        case EventType::ExecuteHidden:
        case EventType::Cross:
        case EventType::Halt: ++stats_.ignored; return;
    }
}

// Recreates the aggressor: an IOC order on the other side, limited to the execution price,
// for exactly the executed size.
void Replayer::execute(const Message& m, Price price) {
    book_.submit(
        NewOrder::limit(nextAggressorId_++, opposite(m.side), m.size, price, TimeInForce::IOC));

    const bool restingIsBuy = m.side == Side::Buy;
    const bool sameFill =
        trades_.size() == 1 &&
        (restingIsBuy ? trades_[0].buyOrderId : trades_[0].sellOrderId) == m.orderId &&
        trades_[0].quantity == m.size && trades_[0].price == price;
    ++(sameFill ? stats_.executionsMatched : stats_.executionsMismatched);
}

}  // namespace ome::lobster
