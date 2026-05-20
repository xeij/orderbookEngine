# lob

A limit order book engine in C++20. Single symbol, single-threaded matcher,
lock-free SPSC inbound queue, ITCH 5.0 replay for validation.

Aiming for sub-microsecond `submit` on a pinned core.

## Build

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

First configure pulls Catch2 and Google Benchmark via `FetchContent`, so it
needs internet. `-DLOB_BUILD_TESTS=OFF` / `-DLOB_BUILD_BENCH=OFF` skip the
dependency fetch.

## Run

Microbenches, pinned to an isolated core:

```sh
taskset -c 3 ./build/bench/lob_bench --benchmark_min_time=2s
```

ITCH replay against one symbol:

```sh
./scripts/download_itch_sample.sh
mkdir -p out
taskset -c 3 ./build/lob_replay \
    --symbol AAPL --tick 100 --top-n 10 --out-dir out \
    data/01302019.NASDAQ_ITCH50
./scripts/plot_latency.py out
./scripts/flamegraph.sh data/01302019.NASDAQ_ITCH50 AAPL   # Linux only
```

The harness prints `count / mean / p50 / p90 / p99 / p99.9 / max` per op and
writes `out/snapshot.csv` — the top-N bid/ask at session close. Diff that
against LOBSTER's published EOD book for the same symbol+day to validate.

## How it's put together

Order objects come out of a fixed-size slab, never `malloc`. Each price level
is an intrusive doubly-linked FIFO whose links live inside `Order`, so a push
or unlink touches only the order itself.

Both sides of the book are a flat `PriceLevel[]` indexed by `price - base`.
A packed bitmap shadows the array; finding the next best after a cancel is a
`countl_zero` / `countr_zero` per 64 levels. At the default 131k levels the
bitmap fits in L1D.

`OrderId → Order*` is an open-addressing table with backshift erase, sized to
the peak open-order count so it never resizes on the hot path.

Matching is a templated loop specialised on aggressor side. All five TIFs are
in: GTC, DAY (= GTC for our session-scoped engine), IOC, FOK, POST_ONLY, plus
MARKET. FOK does a non-destructive pre-walk and rejects if the book can't
fully absorb. POST_ONLY rejects on would-cross. The replay harness uses the
passive primitives (`submit` GTC, `reduce`, `cancel`) — the exchange already
ran its own matching when it wrote the tape.

The SPSC ring is the Disruptor pattern: producer and consumer cursors on
their own cache lines, each side caches the other's last-observed value so
the steady-state push/pop is local-line-only.

## Things worth knowing

The default 131k-level window covers about one dollar-decade per symbol at
penny ticks. Rebasing when a name walks out of its window is not implemented;
for everything except BRK.A you won't notice.

Recorded latencies are wall-clock from `steady_clock` and have ~20 ns of
clock overhead baked in. Subtract before quoting engine numbers.

For repeatable p99s on a laptop, disable Turbo (`cpupower frequency-set -g
performance` and `echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo`) —
otherwise the core reclocks mid-run and the tail is meaningless.
