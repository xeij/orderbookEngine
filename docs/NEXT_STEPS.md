# Bring-up runbook

The four steps to take this from "compiles in theory" to "numbers in the README."
Do them on the Arch laptop, in order. Commands assume you're at the project root.

---

## 1. Build + tests

### Packages

```sh
sudo pacman -S --needed base-devel cmake ninja git python python-matplotlib perf
```

### Configure + build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

The first configure pulls Catch2 v3.5.3 and Google Benchmark v1.8.3 into
`build/_deps/`. Needs ~30 s of internet. If you're offline, configure with
`-DLOB_BUILD_TESTS=OFF -DLOB_BUILD_BENCH=OFF` and skip ahead to the replay
binary, which has no external deps.

### Tests

```sh
ctest --test-dir build --output-on-failure
```

You should see ~50 Catch2 assertions across 9 test files, all green.

If a test fails, re-run just that one:
```sh
./build/tests/lob_tests "Resting limit orders set best bid and ask" -r console
```

I couldn't compile this on Windows, so the first real build is the smoke
test. If you hit a compile error, paste the first ~30 lines back to me and
I'll fix in place. The most likely spots are the templated `match_<Side, Sink>`
dispatch in `order_book.hpp` and the `alignas(kCacheLine)` math in
`spsc_ring.hpp`.

---

## 2. Benchmarks on a quiet core

The microbenches are measuring nanoseconds. On a default-configured laptop,
half of what you'd measure is scheduling jitter, not the engine. Calm the
machine first.

### Identify an isolated core

```sh
lscpu -e
```

Pick a mid-range CPU (3, 4, 5 — not 0 which handles interrupts). Note its
SMT sibling: if `CPU 3` and `CPU 11` share `CORE 3`, you'll want to offline
the sibling.

### Disable Turbo + pin governor

```sh
sudo cpupower frequency-set -g performance
# Intel:
echo 1 | sudo tee /sys/devices/system/cpu/intel_pstate/no_turbo
# AMD:
# echo 0 | sudo tee /sys/devices/system/cpu/cpufreq/boost
```

Verify all CPUs report the same frequency in `/proc/cpuinfo`:
```sh
grep MHz /proc/cpuinfo | sort -u
```
One line out = good. Multiple lines = something's still boosting.

### Offline the SMT sibling of your bench core

```sh
echo 0 | sudo tee /sys/devices/system/cpu/cpu11/online
```
(Re-enable after: `echo 1 | ...`.)

### Run

```sh
taskset -c 3 ./build/bench/lob_bench \
    --benchmark_min_time=2s \
    --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true
```

Output looks like:
```
BM_AddPassive_median       338 ns
BM_AddPassive_stddev        12 ns
BM_AddCancel_median        510 ns
BM_MatchOneAggressive_med  720 ns
BM_ModifyInPlace_median    180 ns
```

Google Benchmark's stddev is over repetitions of the *mean*, not the per-op
tail. So you get good `p50`-equivalent numbers from this, but **for p99/p99.9
use the replay harness in step 3** — that runs through the histogram class
and writes actual percentiles.

### Drop the numbers in the README

Replace the placeholder targets in the README's "How it's put together" area
with a small table:

```
| op            | p50  | p99   |
| ------------- | ---- | ----- |
| submit (rest) | XXns | XXXns |
| submit (match)| XXns | XXXns |
| cancel        | XXns | XXXns |
```

---

## 3. ITCH replay + LOBSTER validation

### Get an ITCH file

```sh
./scripts/download_itch_sample.sh 01302019
```

The script `curl`s + `gunzip`s into `data/`. Final file is 5-7 GB. The
specific date is whatever NASDAQ currently has up at
https://emi.nasdaq.com/ITCH/ — they rotate older files out, so if `01302019`
404s, browse the directory and pick whatever date is listed.

### Get a LOBSTER reference for the same symbol-day

LOBSTER hands out free samples at https://lobsterdata.com/info/DataSamples.php
for a fixed handful of dates and symbols. `2012-06-21` historically covers
AAPL, AMZN, GOOG, INTC, MSFT. If NASDAQ no longer hosts that date's ITCH,
you'll only be able to validate that the book looks *sane* (top of book is
sensible, no crossed quotes, total depth in a normal range) — not diff
exactly.

A LOBSTER sample is two CSVs:
- `AAPL_2012-06-21_..._message_10.csv` — every event
- `AAPL_2012-06-21_..._orderbook_10.csv` — top-10 book after each event

Columns of `orderbook_10`: `ask_p1, ask_s1, bid_p1, bid_s1, ask_p2, ...`.
Prices are in 1/10000-dollar units, same as ITCH.

### Replay

```sh
mkdir -p out
taskset -c 3 ./build/lob_replay \
    --symbol AAPL --tick 100 --top-n 10 --out-dir out \
    data/01302019.NASDAQ_ITCH50
```

You'll see:
```
[replay] parsed N ITCH messages in M ms (X Mmsg/s)
[replay] AAPL adds=... execs=... cancels=... deletes=... replaces=...
submit: count=... mean_ns=... p50=... p99=... p999=...
```

Latency CSVs land in `out/latency_{submit,cancel,reduce}.csv`. EOD snapshot
in `out/snapshot.csv`. Plot:
```sh
./scripts/plot_latency.py out
```

### Diff against LOBSTER

Drop this into `scripts/lobster_diff.py`:

```python
#!/usr/bin/env python3
"""Diff our EOD snapshot against the last row of a LOBSTER orderbook_10.csv."""
import csv, sys, pathlib

mine = pathlib.Path(sys.argv[1])
lob  = pathlib.Path(sys.argv[2])
tick = int(sys.argv[3]) if len(sys.argv) > 3 else 100

my_bid, my_ask = {}, {}
for row in csv.DictReader(mine.open()):
    p = int(row["price"]) * tick
    q = int(row["quantity"])
    (my_bid if row["side"] == "B" else my_ask)[p] = q

last = None
for row in csv.reader(lob.open()):
    last = row
ref_bid, ref_ask = {}, {}
for i in range(0, len(last), 4):
    ap, as_, bp, bs = map(int, last[i:i+4])
    if ap > 0: ref_ask[ap] = as_
    if bp > 0: ref_bid[bp] = bs

def diff(a, b, name, reverse):
    keys = sorted(set(a) | set(b), reverse=reverse)[:10]
    for k in keys:
        if a.get(k, 0) != b.get(k, 0):
            print(f"{name} {k}: mine={a.get(k,0)} lobster={b.get(k,0)}")

diff(my_bid, ref_bid, "bid", reverse=True)
diff(my_ask, ref_ask, "ask", reverse=False)
```

Run:
```sh
python scripts/lobster_diff.py out/snapshot.csv \
    data/AAPL_2012-06-21_..._orderbook_10.csv
```

Empty output = exact match. A handful of differences are expected:
- LOBSTER includes auction (`Q`) prints; our engine ignores them.
- Deep levels (rank 8-10) sometimes drift by one or two orders because the
  engine doesn't model hidden / iceberg orders, and ITCH's `B` (broken
  trade) messages aren't replayed.
- `LULD` halts can leave a few levels stale until trading resumes.

Top three levels both sides should match in price *and* quantity. If they
don't, the bug is in the replay handler (most likely the side-tracking on
`OrderReplace`), not the book.

---

## 4. Flamegraph

### Install

```sh
sudo pacman -S perf
git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph
```

### Kernel permissions

`perf record` with DWARF unwinding needs:
```sh
sudo sysctl kernel.perf_event_paranoid=-1
sudo sysctl kernel.kptr_restrict=0
```
Persist by writing the same lines to `/etc/sysctl.d/99-perf.conf`.

### Record + render

```sh
./scripts/flamegraph.sh data/01302019.NASDAQ_ITCH50 AAPL
```

Output: `flamegraph_AAPL_<timestamp>.svg` in the project root.

### Sanity-check what you see

Open the SVG in a browser. Width = time. You should see roughly:
- `lob::itch::parse` and its callees (parser is bandwidth-bound, can be a
  big share — that's fine)
- `lob::OrderBook::submit` / `cancel` / `reduce`
- A small `__memcpy` / page-in band at the very start (mmap fault-in)

**Red flags** in the flamegraph that mean there's a bug:
- `malloc`, `operator new`, `free` anywhere visible on the hot path. The slab
  should make this impossible during steady state.
- `std::function`, `__dynamic_cast`, vtable thunks. Means a template
  inlining failed somewhere.
- `__pthread_mutex_lock`. Means a hidden lock leaked in.

### Park under docs/ and link

```sh
mkdir -p docs
mv flamegraph_AAPL_*.svg docs/flamegraph.svg
```

Add to the README, near the bottom:

```
## Flamegraph

![CPU flamegraph](docs/flamegraph.svg)

Recorded over a full session of `01302019.NASDAQ_ITCH50` filtered to AAPL,
pinned core, `perf record -F 999 --call-graph dwarf`. Reproduce with
`scripts/flamegraph.sh`.
```

GitHub renders SVGs inline.
