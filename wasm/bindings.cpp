// WebAssembly bindings (Emscripten embind) for the web UI and the MCP server.
//
// Crossing the JS/wasm boundary is the expensive part, so the hot calls use numbers only:
// side and time-in-force are small integer codes, and results come back as flat arrays of
// doubles that JS reads through a Float64Array view of wasm memory (no per-field calls).
// web/src/engine/engine.ts decodes them; wasm/ome.d.ts documents the layouts.
//
// Ids, prices (ticks) and quantities are doubles in JS, checked here to be whole numbers
// below 2^53. A bad argument throws std::invalid_argument, which JS receives as an exception.

#include "ome/order_book.hpp"

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

using emscripten::val;

namespace {

constexpr double kMaxSafeInteger = 9007199254740991.0;  // 2^53 - 1

std::int64_t wholeNumber(double x, const char* what) {
    if (!std::isfinite(x) || x != std::floor(x) || std::fabs(x) > kMaxSafeInteger) {
        throw std::invalid_argument(std::string(what) + " must be a whole number below 2^53");
    }
    return static_cast<std::int64_t>(x);
}

ome::OrderId orderId(double x) {
    const auto id = wholeNumber(x, "order id");
    if (id < 0) throw std::invalid_argument("order id must not be negative");
    return static_cast<ome::OrderId>(id);
}

// Codes shared with engine.ts: side 0 = buy, 1 = sell; tif 0 = GTC, 1 = IOC, 2 = FOK.
ome::Side side(int code) {
    if (code == 0) return ome::Side::Buy;
    if (code == 1) return ome::Side::Sell;
    throw std::invalid_argument("side must be 0 (buy) or 1 (sell)");
}

ome::TimeInForce timeInForce(int code) {
    if (code == 0) return ome::TimeInForce::GTC;
    if (code == 1) return ome::TimeInForce::IOC;
    if (code == 2) return ome::TimeInForce::FOK;
    throw std::invalid_argument("time in force must be 0 (GTC), 1 (IOC) or 2 (FOK)");
}

double num(std::int64_t x) { return static_cast<double>(x); }
double num(std::uint64_t x) { return static_cast<double>(x); }
double code(ome::Side s) { return s == ome::Side::Buy ? 0 : 1; }

// Every event is one fixed-size record of kEventWidth doubles; field 0 is the event type.
// Layouts (see wasm/ome.d.ts):
//   0 accepted  [0, id, side, orderType, price, quantity, sequence, 0]
//   1 trade     [1, buyId, sellId, aggressorSide, price, quantity, 0, 0]
//   2 cancelled [2, id, quantity, reason, 0, 0, 0, 0]
//   3 modified  [3, id, price, quantity, priorityKept, sequence, 0, 0]
//   4 rejected  [4, id, request, reason, 0, 0, 0, 0]
// Enum values (orderType, reason, request) are the C++ enums' underlying values.
constexpr std::size_t kEventWidth = 8;

class EventBuffer : public ome::EventListener {
public:
    std::vector<double> records;

    void onAccepted(const ome::OrderAccepted& e) override {
        push({0, num(e.id), code(e.side), static_cast<double>(e.type), num(e.price),
              num(e.quantity), num(e.sequence), 0});
    }
    void onTrade(const ome::Trade& t) override {
        push({1, num(t.buyOrderId), num(t.sellOrderId), code(t.aggressorSide), num(t.price),
              num(t.quantity), 0, 0});
    }
    void onCancelled(const ome::OrderCancelled& e) override {
        push({2, num(e.id), num(e.cancelledQuantity), static_cast<double>(e.reason), 0, 0, 0, 0});
    }
    void onModified(const ome::OrderModified& e) override {
        push({3, num(e.id), num(e.newPrice), num(e.newQuantity), e.priorityKept ? 1.0 : 0.0,
              num(e.sequence), 0, 0});
    }
    void onRejected(const ome::OrderRejected& e) override {
        push({4, num(e.id), static_cast<double>(e.request), static_cast<double>(e.reason), 0, 0, 0,
              0});
    }

private:
    void push(const std::array<double, kEventWidth>& record) {
        records.insert(records.end(), record.begin(), record.end());
    }
};

// A Float64Array viewing `data` in wasm memory. Valid only until the next call into the
// module (memory can grow and move); engine.ts decodes it immediately.
val view(const std::vector<double>& data) {
    return val(emscripten::typed_memory_view(data.size(), data.data()));
}

// A book plus its event buffer. Each mutating call clears the previous call's events.
class WasmBook {
public:
    WasmBook(const std::string& mode, double lastTradePrice)
        : book_(config(mode, lastTradePrice), events_) {}

    void submitLimit(double id, int s, double quantity, double price, int tif) {
        events_.records.clear();
        book_.submit(ome::NewOrder::limit(orderId(id), side(s), wholeNumber(quantity, "quantity"),
                                          wholeNumber(price, "price"), timeInForce(tif)));
    }
    void submitMarket(double id, int s, double quantity, int tif) {
        events_.records.clear();
        book_.submit(ome::NewOrder::market(orderId(id), side(s), wholeNumber(quantity, "quantity"),
                                           timeInForce(tif)));
    }
    void cancel(double id) {
        events_.records.clear();
        book_.cancel(orderId(id));
    }
    void modify(double id, double price, double quantity) {
        events_.records.clear();
        book_.modify(orderId(id), wholeNumber(price, "price"), wholeNumber(quantity, "quantity"));
    }

    // The last call's events: kEventWidth doubles each.
    val events() const { return view(events_.records); }

    double bestBid() const { return book_.bestBid() ? num(*book_.bestBid()) : NAN; }
    double bestAsk() const { return book_.bestAsk() ? num(*book_.bestAsk()) : NAN; }
    double lastTradePrice() const {
        return book_.lastTradePrice() ? num(*book_.lastTradePrice()) : NAN;
    }
    double orderCount() const { return static_cast<double>(book_.orderCount()); }

    // [price, quantity, orders] per level, best first.
    val depth(int s, double maxLevels) {
        scratch_.clear();
        for (const auto& l :
             book_.depth(side(s), static_cast<std::size_t>(wholeNumber(maxLevels, "levels")))) {
            scratch_.insert(scratch_.end(),
                            {num(l.price), num(l.quantity), static_cast<double>(l.orderCount)});
        }
        return view(scratch_);
    }

    // [side, orderType, price, remaining] of a resting order, or empty if not resting.
    val find(double id) {
        scratch_.clear();
        if (const auto o = book_.find(orderId(id))) {
            scratch_ = {code(o->side), static_cast<double>(o->type), num(o->price),
                        num(o->remaining)};
        }
        return view(scratch_);
    }

private:
    static ome::BookConfig config(const std::string& mode, double lastTradePrice) {
        ome::BookConfig c;
        if (mode == "legacy") {
            c.mode = ome::MatchingMode::Legacy;
        } else if (mode != "modern") {
            throw std::invalid_argument("mode must be 'modern' or 'legacy'");
        }
        if (lastTradePrice > 0) c.lastTradePrice = wholeNumber(lastTradePrice, "last trade price");
        return c;
    }

    EventBuffer events_;  // declared first: the book holds a reference to it
    ome::OrderBook book_;
    std::vector<double> scratch_;  // backing store for depth()/find() views
};

}  // namespace

EMSCRIPTEN_BINDINGS(ome) {
    emscripten::class_<WasmBook>("Book")
        .constructor<const std::string&, double>()
        .function("submitLimit", &WasmBook::submitLimit)
        .function("submitMarket", &WasmBook::submitMarket)
        .function("cancel", &WasmBook::cancel)
        .function("modify", &WasmBook::modify)
        .function("events", &WasmBook::events)
        .function("bestBid", &WasmBook::bestBid)
        .function("bestAsk", &WasmBook::bestAsk)
        .function("lastTradePrice", &WasmBook::lastTradePrice)
        .function("orderCount", &WasmBook::orderCount)
        .function("depth", &WasmBook::depth)
        .function("find", &WasmBook::find);
}
