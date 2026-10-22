// ome-difftest: differential test of OrderBook against the naive ReferenceBook.
//
//   ome-difftest [--ops N] [--seed S] [--seeds K] [--mode legacy|modern|both] [--check-every N]
//
// For each seed and mode, generates N random operations, applies each to both books and
// requires identical events; every `check-every` operations (and at the end) it also
// compares the full books and checks OrderBook's invariants. On the first mismatch it
// prints the seed, the recent operations and the difference, and exits 1.

#include "differential.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace ome;
using namespace ome::test;

namespace {

struct Options {
    std::uint64_t ops = 100'000;
    std::uint64_t firstSeed = 1;
    std::uint64_t seeds = 1;
    std::vector<MatchingMode> modes{MatchingMode::Legacy, MatchingMode::Modern};
    std::uint64_t checkEvery = 64;
};

// What the random streams actually exercised, so a "pass" can't hide a generator that
// never reaches the interesting paths.
struct Stats {
    std::map<std::string, std::uint64_t> events;  // event kind (and reason) -> count
    std::size_t maxResting = 0;
    std::uint64_t checksWithRestingMarket = 0;  // state checks that saw a legacy market order
    std::uint64_t checks = 0;

    void add(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            // "trade ...", "cancelled #1 qty=3 (reason)", "rejected #1 new (reason)", ...
            std::string key = line.substr(0, line.find(' '));
            if (const auto open = line.find('('); open != std::string::npos) {
                key += " " + line.substr(open);
            }
            if (key == "modified") {
                key += line.find(" kept") != std::string::npos ? " (kept priority)"
                                                               : " (lost priority)";
            }
            ++events[key];
        }
    }
};

bool run(MatchingMode mode, std::uint64_t seed, const Options& opts, std::uint64_t& ops,
         Stats& stats) {
    DifferentialRunner runner({mode, Price{10'000}});
    OpGenerator gen(seed, runner.book());
    std::deque<std::string> recent;  // last few ops, for the failure report

    auto fail = [&](std::uint64_t i, const std::string& what) {
        std::cerr << "MISMATCH mode=" << toString(mode) << " seed=" << seed << " op=" << i << '\n'
                  << "recent operations:\n";
        for (const auto& r : recent) std::cerr << "  " << r << '\n';
        std::cerr << what;
        return false;
    };

    for (std::uint64_t i = 0; i < opts.ops; ++i) {
        const Op op = gen.next();
        recent.push_back(op.describe());
        if (recent.size() > 20) recent.pop_front();

        if (auto diff = runner.apply(op); !diff.empty()) return fail(i, diff);
        ++ops;
        stats.add(runner.lastEvents());
        stats.maxResting = std::max(stats.maxResting, runner.book().orderCount());
        if ((i + 1) % opts.checkEvery == 0 || i + 1 == opts.ops) {
            if (auto diff = runner.checkState(); !diff.empty()) return fail(i, diff);
            ++stats.checks;
            for (Side side : {Side::Buy, Side::Sell}) {
                const auto orders = runner.book().orders(side);
                if (!orders.empty() && orders.front().type == OrderType::Market) {
                    ++stats.checksWithRestingMarket;
                }
            }
        }
    }
    return true;
}

int run(int argc, char** argv) {
    Options opts;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string arg = argv[i];
        const std::string value = argv[i + 1];
        if (arg == "--ops") {
            opts.ops = std::strtoull(value.c_str(), nullptr, 10);
        } else if (arg == "--seed") {
            opts.firstSeed = std::strtoull(value.c_str(), nullptr, 10);
        } else if (arg == "--seeds") {
            opts.seeds = std::strtoull(value.c_str(), nullptr, 10);
        } else if (arg == "--check-every") {
            opts.checkEvery = std::max<std::uint64_t>(1, std::strtoull(value.c_str(), nullptr, 10));
        } else if (arg == "--mode") {
            if (value == "legacy") {
                opts.modes = {MatchingMode::Legacy};
            } else if (value == "modern") {
                opts.modes = {MatchingMode::Modern};
            } else if (value != "both") {
                std::cerr << "bad --mode\n";
                return 2;
            }
        } else {
            std::cerr << "unknown option " << arg << '\n';
            return 2;
        }
    }

    const auto start = std::chrono::steady_clock::now();
    std::uint64_t ops = 0;
    Stats stats;
    for (std::uint64_t s = opts.firstSeed; s < opts.firstSeed + opts.seeds; ++s) {
        for (MatchingMode mode : opts.modes) {
            if (!run(mode, s, opts, ops, stats)) return 1;
        }
    }
    const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
    std::cout << "ok: " << ops << " operations (" << opts.seeds << " seed(s) x "
              << opts.modes.size() << " mode(s)) identical in both books, " << elapsed.count()
              << " s\n";
    std::cout << "max resting orders: " << stats.maxResting << "\nstate checks: " << stats.checks
              << " (" << stats.checksWithRestingMarket << " saw a resting market order)\nevents:\n";
    for (const auto& [kind, count] : stats.events)
        std::cout << "  " << kind << ": " << count << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ome-difftest: " << e.what() << '\n';
        return 1;
    }
}
