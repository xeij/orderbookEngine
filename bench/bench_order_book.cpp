// Microbenchmarks for the matching engine.
//
// Build (Release, native-arch):
//   cmake -B build-bench -DCMAKE_BUILD_TYPE=Release && cmake --build build-bench -j
// Run pinned to one core for clean numbers:
//   taskset -c 3 ./build-bench/bench/lob_bench --benchmark_min_time=1s
//
// All benches construct a freshly-warmed book in their fixture, then time the
// single operation in a tight loop. We use benchmark::DoNotOptimize on the
// result so the compiler can't elide the call after observing the NullSink.

#include <benchmark/benchmark.h>

#include <random>
#include <vector>

#include "lob/order_book.hpp"

namespace {

constexpr lob::Price kBase    = 0;
constexpr std::size_t kLevels = std::size_t{1} << 17;
constexpr std::size_t kSlots  = std::size_t{1} << 20;

lob::OrderBook fresh_book() {
    return lob::OrderBook{lob::OrderBook::Config{kBase, kLevels, kSlots}};
}

// Build a book preloaded with `depth` orders on each of `levels_per_side`
// levels around a target mid. Returns the mid price.
lob::Price preload(lob::OrderBook& book, lob::OrderId& next_id,
                   lob::Price mid, std::size_t levels_per_side,
                   std::size_t depth_per_level) {
    lob::NullSink sink;
    for (std::size_t d = 0; d < depth_per_level; ++d) {
        for (std::size_t l = 0; l < levels_per_side; ++l) {
            const lob::Price bid_price = mid - static_cast<lob::Price>(l) - 1;
            const lob::Price ask_price = mid + static_cast<lob::Price>(l) + 1;
            book.submit(next_id++, lob::Side::Buy,  lob::OrderType::Limit,
                        lob::TimeInForce::GTC, bid_price, 100, 0, sink);
            book.submit(next_id++, lob::Side::Sell, lob::OrderType::Limit,
                        lob::TimeInForce::GTC, ask_price, 100, 0, sink);
        }
    }
    return mid;
}

}  // namespace

// Passive add: no crossing, deep book. Measures the pure insert path
// (slab alloc + intrusive push + bitmap maybe-set + id-map insert).
static void BM_AddPassive(benchmark::State& state) {
    auto book = fresh_book();
    lob::OrderId next_id = 1;
    preload(book, next_id, 10000, 50, 10);
    lob::NullSink sink;

    std::mt19937_64 rng(0xC0FFEE);
    while (state.KeepRunning()) {
        const lob::Price p = 10000 - static_cast<lob::Price>(rng() % 50) - 1;
        benchmark::DoNotOptimize(book.submit(
            next_id++, lob::Side::Buy, lob::OrderType::Limit,
            lob::TimeInForce::GTC, p, 100, 0, sink));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(BM_AddPassive);

// Steady-state add then cancel: the realistic shape of a churning book where
// orders arrive and are pulled within microseconds.
static void BM_AddCancel(benchmark::State& state) {
    auto book = fresh_book();
    lob::OrderId next_id = 1;
    preload(book, next_id, 10000, 50, 10);
    lob::NullSink sink;

    while (state.KeepRunning()) {
        const lob::OrderId id = next_id++;
        book.submit(id, lob::Side::Buy, lob::OrderType::Limit,
                    lob::TimeInForce::GTC, 9950, 100, 0, sink);
        benchmark::DoNotOptimize(book.cancel(id));
    }
    state.SetItemsProcessed(state.iterations() * 2);  // two ops per iter
}
BENCHMARK(BM_AddCancel);

// Aggressive limit that consumes exactly one resting order, then the next
// passive add refills. Measures the match+trade-emit hot path.
static void BM_MatchOneAggressive(benchmark::State& state) {
    auto book = fresh_book();
    lob::OrderId next_id = 1;
    preload(book, next_id, 10000, 50, 10);
    lob::NullSink sink;

    while (state.KeepRunning()) {
        // Add a resting sell at the ask, then cross it with a buy.
        const lob::OrderId resting = next_id++;
        book.submit(resting, lob::Side::Sell, lob::OrderType::Limit,
                    lob::TimeInForce::GTC, 10050, 100, 0, sink);
        benchmark::DoNotOptimize(book.submit(
            next_id++, lob::Side::Buy, lob::OrderType::Limit,
            lob::TimeInForce::IOC, 10050, 100, 0, sink));
    }
}
BENCHMARK(BM_MatchOneAggressive);

// Quantity-down modify at same price -- the in-place fast path.
static void BM_ModifyInPlace(benchmark::State& state) {
    auto book = fresh_book();
    lob::OrderId next_id = 1;
    preload(book, next_id, 10000, 50, 10);
    lob::NullSink sink;

    const lob::OrderId pinned = next_id++;
    book.submit(pinned, lob::Side::Buy, lob::OrderType::Limit,
                lob::TimeInForce::GTC, 9950, 1'000'000, 0, sink);

    lob::Quantity q = 999'999;
    while (state.KeepRunning()) {
        benchmark::DoNotOptimize(book.modify(pinned, q, 9950, 0, sink));
        // ratchet down to keep the in-place fast path valid; reset before exhaustion
        if (--q == 0) {
            book.cancel(pinned);
            book.submit(pinned, lob::Side::Buy, lob::OrderType::Limit,
                        lob::TimeInForce::GTC, 9950, 1'000'000, 0, sink);
            q = 999'999;
        }
    }
}
BENCHMARK(BM_ModifyInPlace);

// Cancel of the best-bid order, forcing a bitmap scan to recover the next-
// best. The worst case for cancel.
static void BM_CancelBestForcesScan(benchmark::State& state) {
    auto book = fresh_book();
    lob::OrderId next_id = 1;
    lob::NullSink sink;
    // Sparse book -- 16 bids spaced 64 ticks apart so the bitmap scan must
    // hop across a full 64-bit word on every cancel.
    std::vector<lob::OrderId> bids;
    for (int i = 0; i < 16; ++i) {
        const lob::OrderId id = next_id++;
        bids.push_back(id);
        book.submit(id, lob::Side::Buy, lob::OrderType::Limit,
                    lob::TimeInForce::GTC, 10000 - i * 64, 100, 0, sink);
    }
    std::size_t pop = 0;
    while (state.KeepRunning()) {
        if (pop >= bids.size()) {
            // Refill in reverse so the next pop is again the best.
            for (auto it = bids.rbegin(); it != bids.rend(); ++it) {
                book.submit(*it, lob::Side::Buy, lob::OrderType::Limit,
                            lob::TimeInForce::GTC,
                            10000 - static_cast<lob::Price>(it - bids.rbegin()) * 64,
                            100, 0, sink);
            }
            pop = 0;
        }
        benchmark::DoNotOptimize(book.cancel(bids[pop++]));
    }
}
BENCHMARK(BM_CancelBestForcesScan);
