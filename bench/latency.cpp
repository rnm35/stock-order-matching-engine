// ome-latency: per-request latency percentiles.
//
//   ome-latency [--depth N] [--ops N] [--runs N] [--seed S] [--pin]
//
// Builds the same workload as the throughput benchmark, prefills a book to ~N resting
// orders, then times every measured request individually with the CPU timestamp counter
// (`steady_clock` on Windows ticks only every 100 ns, too coarse for these operations).
// The first run is a warm-up and is discarded; samples from the remaining runs are pooled.
// Reported times include the timer's own overhead, which is printed separately.

#include "workload.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#define OME_HAVE_TSC 1
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX  // keep <windows.h> from defining min/max macros
#endif
#include <windows.h>
#endif

using namespace ome;
using namespace ome::bench;

namespace {

// lfence keeps the timed instructions from being reordered across the timestamp reads.
inline std::uint64_t now() {
#ifdef OME_HAVE_TSC
    _mm_lfence();
    const std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

// Nanoseconds per `now()` tick, measured against steady_clock over ~200 ms.
double nanosPerTick() {
#ifdef OME_HAVE_TSC
    using Clock = std::chrono::steady_clock;
    const auto t0 = Clock::now();
    const std::uint64_t c0 = now();
    while (Clock::now() - t0 < std::chrono::milliseconds(200)) {
    }
    const std::uint64_t c1 = now();
    const std::chrono::duration<double, std::nano> elapsed = Clock::now() - t0;
    return elapsed.count() / static_cast<double>(c1 - c0);
#else
    return 1e9 * std::chrono::steady_clock::period::num / std::chrono::steady_clock::period::den;
#endif
}

// Pins the thread to one core and raises its priority to cut scheduler noise.
void pinThread() {
#ifdef _WIN32
    SetThreadAffinityMask(GetCurrentThread(), 1U << 2U);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
#endif
}

enum Category : std::uint8_t { kLimit, kMarket, kCancel, kModify, kCategories };
constexpr std::array<const char*, kCategories> kNames{"limit", "market", "cancel", "modify"};

Category categoryOf(const Request& r) {
    switch (r.kind) {
        case Request::Kind::Submit: return r.order.type == OrderType::Market ? kMarket : kLimit;
        case Request::Kind::Cancel: return kCancel;
        case Request::Kind::Modify: return kModify;
    }
    return kLimit;
}

void printRow(const char* name, std::vector<std::uint64_t>& ticks, double nsPerTick) {
    if (ticks.empty()) return;
    std::ranges::sort(ticks);
    auto pct = [&](double p) {
        const auto i = static_cast<std::size_t>(p / 100.0 * static_cast<double>(ticks.size() - 1));
        return static_cast<double>(ticks[i]) * nsPerTick;
    };
    double sum = 0;
    for (const auto t : ticks) sum += static_cast<double>(t);
    std::printf("%-8s %10zu %8.0f %8.0f %8.0f %8.0f %8.0f %9.0f %10.0f\n", name, ticks.size(),
                sum / static_cast<double>(ticks.size()) * nsPerTick, pct(50), pct(90), pct(99),
                pct(99.9), pct(99.99), static_cast<double>(ticks.back()) * nsPerTick);
}

std::uint64_t parseNumber(std::string_view text) {
    std::uint64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument("not a number: " + std::string(text));
    }
    return value;
}

int run(int argc, char** argv) {
    WorkloadConfig config;
    config.ops = 1'000'000;
    std::uint64_t runs = 5;
    bool pin = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--pin") {
            pin = true;
        } else if (arg == "--depth" && hasValue) {
            config.restingOrders = parseNumber(argv[++i]);
        } else if (arg == "--ops" && hasValue) {
            config.ops = parseNumber(argv[++i]);
        } else if (arg == "--runs" && hasValue) {
            runs = std::max<std::uint64_t>(2, parseNumber(argv[++i]));
        } else if (arg == "--seed" && hasValue) {
            config.seed = parseNumber(argv[++i]);
        } else {
            std::cerr << "usage: ome-latency [--depth N] [--ops N] [--runs N] [--seed S] [--pin]\n";
            return 2;
        }
    }
    if (pin) pinThread();

    const Workload w = makeWorkload(config);
    const double nsPerTick = nanosPerTick();

    std::array<std::vector<std::uint64_t>, kCategories> samples;
    std::vector<std::uint64_t> all;
    for (std::uint64_t r = 0; r < runs; ++r) {
        EventListener listener;
        OrderBook book({MatchingMode::Modern, std::nullopt}, listener);
        for (const auto& req : w.prefill) apply(book, req);

        const bool warmup = r == 0;
        for (const auto& req : w.ops) {
            const std::uint64_t t0 = now();
            apply(book, req);
            const std::uint64_t t1 = now();
            if (!warmup) {
                samples[categoryOf(req)].push_back(t1 - t0);
                all.push_back(t1 - t0);
            }
        }
    }

    std::vector<std::uint64_t> overhead;  // an empty timed region
    for (int i = 0; i < 1'000'000; ++i) {
        const std::uint64_t t0 = now();
        const std::uint64_t t1 = now();
        overhead.push_back(t1 - t0);
    }

    std::printf("depth ~%zu resting orders, %zu measured requests x %llu runs (+1 warm-up)%s\n",
                config.restingOrders, w.ops.size(), static_cast<unsigned long long>(runs - 1),
                pin ? ", pinned" : "");
    std::printf("%-8s %10s %8s %8s %8s %8s %8s %9s %10s   (ns)\n", "request", "count", "mean",
                "p50", "p90", "p99", "p99.9", "p99.99", "max");
    for (int c = 0; c < kCategories; ++c) printRow(kNames[c], samples[c], nsPerTick);
    printRow("all", all, nsPerTick);
    printRow("timer", overhead, nsPerTick);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(argc, argv);
    } catch (const std::exception& e) {
        std::cerr << "ome-latency: " << e.what() << '\n';
        return 1;
    }
}
