// Times the matching engine on an order file in the original input format.
//
//   ome-replay <input> [--mode legacy|modern] [--repeat N]
//
// The file is parsed up front; only the submit() loop is timed, so the number reflects
// the book, not file I/O or output formatting. It is the like-for-like comparison with the
// original program; ome-bench and ome-latency are the detailed benchmarks.

#include "ome/order_book.hpp"
#include "ome/text_format.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct CountingListener : ome::EventListener {
    std::uint64_t trades = 0;
    void onTrade(const ome::Trade&) override { ++trades; }
};

int run(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ome-replay <input> [--mode legacy|modern] [--repeat N]\n";
        return 1;
    }
    ome::MatchingMode mode = ome::MatchingMode::Legacy;
    int repeat = 5;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string arg = argv[i];
        if (arg == "--mode")
            mode = std::string(argv[i + 1]) == "modern" ? ome::MatchingMode::Modern
                                                        : ome::MatchingMode::Legacy;
        if (arg == "--repeat") {
            const std::string_view value = argv[i + 1];
            const auto [end, ec] =
                std::from_chars(value.data(), value.data() + value.size(), repeat);
            if (ec != std::errc{} || end != value.data() + value.size() || repeat < 1) {
                std::cerr << "--repeat needs a positive integer\n";
                return 1;
            }
        }
    }

    std::ifstream in(argv[1]);
    std::string line;
    if (!in || !std::getline(in, line)) {
        std::cerr << "cannot read " << argv[1] << '\n';
        return 1;
    }
    const auto lastTraded =
        ome::text::parsePrice(line.substr(0, line.find_last_not_of(" \t\r") + 1));
    if (!lastTraded) {
        std::cerr << "bad last traded price\n";
        return 1;
    }

    std::vector<ome::NewOrder> orders;
    while (std::getline(in, line)) {
        const auto r = ome::text::parseOrderLine(line);
        if (r.status != ome::text::LineResult::Status::Ok) continue;
        const ome::OrderId id = orders.size();
        orders.push_back(
            r.order.price ? ome::NewOrder::limit(id, r.order.side, r.order.quantity, *r.order.price)
                          : ome::NewOrder::market(id, r.order.side, r.order.quantity));
    }

    double best = 1e300;
    std::uint64_t trades = 0;
    for (int run = 0; run < repeat; ++run) {
        CountingListener listener;
        ome::OrderBook book({mode, lastTraded}, listener);
        const auto start = std::chrono::steady_clock::now();
        for (const auto& o : orders) book.submit(o);
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        best = std::min(best, elapsed.count());
        trades = listener.trades;
    }

    std::cout << "orders: " << orders.size() << "  trades: " << trades
              << "  mode: " << ome::toString(mode) << "  best of " << repeat << ": " << best * 1e3
              << " ms  (" << static_cast<double>(orders.size()) / best << " orders/sec)\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ome-replay: " << e.what() << '\n';
        return 1;
    }
}
