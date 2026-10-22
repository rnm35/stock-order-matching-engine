// ome-lobster: replays a LOBSTER message file through the book (see ome/lobster.hpp).
//
//   ome-lobster <..._message_N.csv> [--orderbook <..._orderbook_N.csv>] [--limit N]
//
// Pass 1 replays the messages alone: it is timed (best of 5) and shows what the raw file
// gives. With --orderbook, pass 2 replays again and reconciles our book with the true best
// bid/ask after every message, removing orders whose deletion the file left out. Both
// report how many executions our book reproduced exactly (same resting order, same size).

#include "ome/lobster.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ome::lobster::Message;
using ome::lobster::ReplayStats;
using ome::lobster::TopOfBook;

unsigned long long u(std::uint64_t n) { return static_cast<unsigned long long>(n); }

double percent(std::uint64_t part, std::uint64_t whole) {
    return whole == 0 ? 0.0 : 100.0 * static_cast<double>(part) / static_cast<double>(whole);
}

void printExecutions(const char* label, const ReplayStats& s) {
    const auto checked = s.executionsMatched + s.executionsMismatched;
    std::printf("%s\n", label);
    std::printf("  executions reproduced exactly  %llu / %llu (%.2f%%)\n", u(s.executionsMatched),
                u(checked), percent(s.executionsMatched, checked));
    std::printf("  executions of unknown orders   %llu (not checkable)\n", u(s.unknownOrder));
    std::printf("  new orders that crossed our book %llu\n", u(s.submitsThatTraded));
}

int run(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: ome-lobster <message.csv> [--orderbook <orderbook.csv>] [--limit N]\n";
        return 2;
    }
    std::string orderbookPath;
    std::uint64_t limit = UINT64_MAX;
    for (int i = 2; i + 1 < argc; i += 2) {
        const std::string_view arg = argv[i];
        const std::string_view value = argv[i + 1];
        if (arg == "--orderbook") {
            orderbookPath = value;
        } else if (arg == "--limit") {
            const auto [end, ec] =
                std::from_chars(value.data(), value.data() + value.size(), limit);
            if (ec != std::errc{} || end != value.data() + value.size()) {
                std::cerr << "--limit needs a number\n";
                return 2;
            }
        } else {
            std::cerr << "unknown option " << arg << '\n';
            return 2;
        }
    }

    std::ifstream in(argv[1]);
    if (!in) {
        std::cerr << "cannot open " << argv[1] << '\n';
        return 1;
    }
    std::ifstream book;
    if (!orderbookPath.empty()) {
        book.open(orderbookPath);
        if (!book) {
            std::cerr << "cannot open " << orderbookPath << '\n';
            return 1;
        }
    }

    // Line i of the orderbook file is the book after line i of the message file.
    std::vector<Message> messages;
    std::vector<TopOfBook> tops;
    std::uint64_t malformed = 0;
    std::string line;
    std::string bookLine;
    while (messages.size() < limit && std::getline(in, line)) {
        std::optional<TopOfBook> top;
        if (book.is_open()) {
            if (std::getline(book, bookLine)) top = ome::lobster::parseOrderbookRow(bookLine);
            if (!top) {
                std::cerr << "orderbook file doesn't line up with the message file\n";
                return 1;
            }
        }
        if (const auto m = ome::lobster::parseMessage(line)) {
            messages.push_back(*m);
            if (top) tops.push_back(*top);
        } else {
            ++malformed;
        }
    }

    // Pass 1: messages only, timed.
    double best = 1e300;
    ReplayStats raw;
    for (int run = 0; run < 5; ++run) {
        ome::lobster::Replayer replayer;
        const auto start = std::chrono::steady_clock::now();
        for (const auto& m : messages) replayer.apply(m);
        const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
        best = std::min(best, elapsed.count());
        raw = replayer.stats();
    }

    std::printf("messages             %llu (+%llu malformed lines skipped)\n", u(raw.messages),
                u(malformed));
    std::printf("  new orders         %llu\n", u(raw.submits));
    std::printf("  partial cancels    %llu\n", u(raw.partialCancels));
    std::printf("  deletes            %llu\n", u(raw.deletes));
    std::printf("  visible executions %llu\n", u(raw.executions));
    std::printf("  ignored            %llu (hidden executions, cross trades, halts)\n",
                u(raw.ignored));
    std::printf("sub-tick prices      %llu\n", u(raw.subTickPrice));
    std::printf("replay               %.2f ms, %.2f M messages/s (messages only, best of 5)\n",
                best * 1e3, static_cast<double>(raw.messages) / best / 1e6);
    printExecutions("messages only:", raw);

    if (!tops.empty()) {
        ome::lobster::Replayer replayer;
        for (std::size_t i = 0; i < messages.size(); ++i) {
            replayer.apply(messages[i]);
            replayer.reconcile(tops[i]);
        }
        const auto& s = replayer.stats();
        printExecutions("reconciled with the orderbook file after every message:", s);
        std::printf("  stale orders removed           %llu\n", u(s.staleOrdersRemoved));
        std::printf("  best bid+ask (price, size) right %llu / %llu (%.2f%%)\n",
                    u(s.topOfBookMatched), u(s.topOfBookChecked),
                    percent(s.topOfBookMatched, s.topOfBookChecked));
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ome-lobster: " << e.what() << '\n';
        return 1;
    }
}
