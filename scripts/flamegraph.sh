#!/usr/bin/env bash
# Record a CPU flamegraph of the replay binary processing an ITCH file.
#
# Prerequisites (Arch):
#   sudo pacman -S perf
#   git clone https://github.com/brendangregg/FlameGraph ~/FlameGraph
#
# Usage:
#   scripts/flamegraph.sh PATH_TO_ITCH_FILE [SYMBOL]
#
# Output: flamegraph_<symbol>_<timestamp>.svg in the project root.

set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "usage: $0 ITCH_FILE [SYMBOL]" >&2
    exit 2
fi

ITCH_FILE="$1"
SYMBOL="${2:-AAPL}"
FLAMEGRAPH_DIR="${FLAMEGRAPH_DIR:-$HOME/FlameGraph}"

if [[ ! -d "$FLAMEGRAPH_DIR" ]]; then
    echo "FlameGraph dir not found at $FLAMEGRAPH_DIR" >&2
    echo "  git clone https://github.com/brendangregg/FlameGraph $FLAMEGRAPH_DIR" >&2
    exit 1
fi

PROJ_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$PROJ_ROOT/build-release}"
REPLAY_BIN="$BUILD_DIR/lob_replay"

if [[ ! -x "$REPLAY_BIN" ]]; then
    echo "lob_replay not built. Build with:" >&2
    echo "  cmake -B $BUILD_DIR -DCMAKE_BUILD_TYPE=Release && cmake --build $BUILD_DIR -j" >&2
    exit 1
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
OUT_SVG="$PROJ_ROOT/flamegraph_${SYMBOL}_${STAMP}.svg"
PERF_DATA="$(mktemp -d)/perf.data"

echo "[flame] recording perf data ..."
# -F 999 = 999 Hz sampling. --call-graph dwarf gives accurate stacks for the
# inlined hot path (without DWARF you only see outermost frames).
perf record -F 999 --call-graph dwarf -o "$PERF_DATA" -- \
    "$REPLAY_BIN" --symbol "$SYMBOL" --snapshot-only --out-dir "$(mktemp -d)" \
    "$ITCH_FILE"

echo "[flame] folding stacks ..."
perf script -i "$PERF_DATA" \
    | "$FLAMEGRAPH_DIR/stackcollapse-perf.pl" \
    | "$FLAMEGRAPH_DIR/flamegraph.pl" --title "lob_replay $SYMBOL" \
    > "$OUT_SVG"

echo "[flame] wrote $OUT_SVG"
