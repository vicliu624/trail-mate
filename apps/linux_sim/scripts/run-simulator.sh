#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build/simulator}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
SCALE="${SIM_SCALE:-1}"
AUTO_EXIT_MS="${SIM_AUTO_EXIT_MS:-0}"

bash "$ROOT_DIR/scripts/build-simulator.sh" "$BUILD_DIR"

SIM_BIN="$BUILD_DIR/trailmate_cardputer_zero_simulator"
if [[ ! -x "$SIM_BIN" ]]; then
  SIM_BIN="$BUILD_DIR/$BUILD_TYPE/trailmate_cardputer_zero_simulator"
fi

if [[ "$AUTO_EXIT_MS" -gt 0 ]]; then
  "$SIM_BIN" --scale "$SCALE" --auto-exit-ms "$AUTO_EXIT_MS"
else
  "$SIM_BIN" --scale "$SCALE"
fi
