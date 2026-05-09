#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${1:-$ROOT_DIR/build/device}"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
FBDEV="${TRAIL_MATE_FBDEV:-/dev/fb0}"

bash "$ROOT_DIR/scripts/build-device.sh" "$BUILD_DIR"

DEVICE_BIN="$BUILD_DIR/trailmate_cardputer_zero_device"
if [[ ! -x "$DEVICE_BIN" ]]; then
  DEVICE_BIN="$BUILD_DIR/$BUILD_TYPE/trailmate_cardputer_zero_device"
fi

"$DEVICE_BIN" --fbdev "$FBDEV"
