#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build/simulator}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBUILD_TESTING=ON

cmake --build "$BUILD_DIR" --target trailmate_cardputer_zero_simulator --config "$BUILD_TYPE"
