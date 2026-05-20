// Microbenchmark for the SPSC ring. Single-threaded shape: time the cost of
// one push followed by one pop on the same core. The cross-core throughput
// number is more interesting but harder to express via Google Benchmark; for
// that the tests/test_spsc_ring.cpp threaded test is the closest analogue.

#include <benchmark/benchmark.h>

#include "lob/spsc_ring.hpp"

namespace {

struct Msg {
    std::uint64_t a;
    std::uint64_t b;
};

}  // namespace

static void BM_SpscRing_PushPop(benchmark::State& state) {
    lob::SpscRing<Msg> ring(1u << 16);
    Msg in {0xdeadbeef, 0x12345678};
    Msg out{};
    std::uint64_t i = 0;
    while (state.KeepRunning()) {
        in.a = ++i;
        benchmark::DoNotOptimize(ring.try_push(in));
        benchmark::DoNotOptimize(ring.try_pop(out));
    }
}
BENCHMARK(BM_SpscRing_PushPop);

BENCHMARK_MAIN();
