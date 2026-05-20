#!/usr/bin/env bash
# Downloads a NASDAQ TotalView-ITCH 5.0 sample from emi.nasdaq.com.
# These files are made available by NASDAQ for free under the standard
# "sample data" license; check https://emi.nasdaq.com/ITCH/ for the
# current directory listing and pick a recent date.
#
# Usage:
#   scripts/download_itch_sample.sh [YYYYMMDD]
# Default date is 2019-01-30 -- NASDAQ has historically kept this file
# around for testing; if it has been rotated out, pass any date that is
# listed on emi.nasdaq.com.

set -euo pipefail

DATE="${1:-01302019}"
BASE_URL="https://emi.nasdaq.com/ITCH"
FILENAME="${DATE}.NASDAQ_ITCH50.gz"
OUT_DIR="$(cd "$(dirname "$0")/.." && pwd)/data"

mkdir -p "$OUT_DIR"
echo "[itch] downloading $FILENAME -> $OUT_DIR/"
curl -fL --progress-bar "${BASE_URL}/${FILENAME}" -o "${OUT_DIR}/${FILENAME}"

echo "[itch] decompressing"
gunzip -kf "${OUT_DIR}/${FILENAME}"

echo "[itch] done. raw file at ${OUT_DIR}/${FILENAME%.gz}"
echo "[itch] sample size:"
du -h "${OUT_DIR}/${FILENAME%.gz}"
