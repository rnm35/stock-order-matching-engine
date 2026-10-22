// Throughput benchmarks (Google Benchmark). Per-request latency percentiles come from
// ome-latency (latency.cpp), which times every request individually.
//
// Run with repetitions and report medians, e.g.
//   ome-bench --benchmark_repetitions=5 --benchmark_report_aggregates_only=true

#include "workload.hpp"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <memory_resource>
#include <vector>

using namespace ome;
using namespace ome::bench;

namespace {

constexpr std::size_t kMeasuredOps = 1'000'000;

// Workloads are expensive to generate, so build each one once per process.
const Workload& workload(std::size_t restingOrders) {
    static std::map<std::size_t, Workload> cache;
    auto it = cache.find(restingOrders);
    if (it == cache.end()) {
        WorkloadConfig config;
        config.restingOrders = restingOrders;
        config.ops = kMeasuredOps;
        it = cache.emplace(restingOrders, makeWorkload(config)).first;
    }
    return it->second;
}

std::unique_ptr<OrderBook> prefilledBook(const Workload& w, EventListener& listener) {
    auto book =
        std::make_unique<OrderBook>(BookConfig{MatchingMode::Modern, std::nullopt}, listener);
    for (const auto& r : w.prefill) apply(*book, r);
    return book;
}

// The headline number: a realistic request mix (adds, cancels, modifies, aggressive and
// market orders) against a book holding ~N resting orders. Building the book and tearing
// it down are not timed.
void BM_MixedWorkload(benchmark::State& state) {
    const Workload& w = workload(static_cast<std::size_t>(state.range(0)));
    EventListener listener;
    for (auto _ : state) {
        state.PauseTiming();
        auto book = prefilledBook(w, listener);
        state.ResumeTiming();

        for (const auto& r : w.ops) apply(*book, r);

        state.PauseTiming();
        book.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(w.ops.size()));
}
BENCHMARK(BM_MixedWorkload)->Arg(1'000)->Arg(10'000)->Arg(100'000)->Unit(benchmark::kMillisecond);

// Adds a passive order at an existing price level and cancels it again: the cost of the
// level lookup, the queue append, the index insert, and the O(1) cancel.
void BM_AddThenCancel(benchmark::State& state) {
    const Workload& w = workload(static_cast<std::size_t>(state.range(0)));
    EventListener listener;
    auto book = prefilledBook(w, listener);
    const auto bids = book->depth(Side::Buy);

    OrderId id = 1'000'000'000;
    std::size_t level = 0;
    for (auto _ : state) {
        book->submit(NewOrder::limit(id, Side::Buy, 10, bids[level].price));
        book->cancel(id);
        ++id;
        level = (level + 1) % bids.size();
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddThenCancel)->Arg(1'000)->Arg(100'000);

// A small market order that partially fills the order at the front of the best level:
// the matching path without any level or order removal.
void BM_MarketOrderMatch(benchmark::State& state) {
    EventListener listener;
    OrderBook book({MatchingMode::Modern, std::nullopt}, listener);
    book.submit(NewOrder::limit(1, Side::Sell, Quantity{1'000'000'000'000}, 10'000));
    OrderId id = 2;
    for (auto _ : state) book.submit(NewOrder::market(id++, Side::Buy, 1));
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_MarketOrderMatch);

// Cost of finding an existing price level in the std::map, with as many levels as the
// mixed workload's book has. This is the part of an add that flat per-tick level arrays
// would replace, so it bounds what that optimisation could save.
void BM_LevelLookup(benchmark::State& state) {
    std::pmr::unsynchronized_pool_resource pool;
    BookSide side(Side::Buy, &pool);
    const auto levels = static_cast<Price>(state.range(0));
    for (Price p = 0; p < levels; ++p) side.levelAt(10'000 - p);

    std::uint64_t x = 1;
    for (auto _ : state) {
        x = x * 6364136223846793005ULL + 1442695040888963407ULL;  // cheap LCG for prices
        const Price price =
            10'000 - static_cast<Price>((x >> 33U) % static_cast<std::uint64_t>(levels));
        benchmark::DoNotOptimize(side.levelAt(price));
    }
}
BENCHMARK(BM_LevelLookup)->Arg(100)->Arg(200)->Arg(1'000);

}  // namespace

BENCHMARK_MAIN();
