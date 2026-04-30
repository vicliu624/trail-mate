#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build/device}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

cmake --build "$BUILD_DIR" --target trailmate_cardputer_zero_device --config "$BUILD_TYPE"
