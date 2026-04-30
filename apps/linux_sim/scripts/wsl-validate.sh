#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT_DIR/../.." && pwd)"
BUILD_TYPE="${BUILD_TYPE:-Debug}"
TEST_BUILD_DIR="${WSL_TEST_BUILD_DIR:-$ROOT_DIR/build/wsl/test}"
SIM_BUILD_DIR="${WSL_SIM_BUILD_DIR:-$ROOT_DIR/build/wsl/simulator}"
RPI_ROOT_DIR="$(cd "$ROOT_DIR/../linux_rpi" && pwd)"
DEVICE_BUILD_DIR="${WSL_DEVICE_BUILD_DIR:-$RPI_ROOT_DIR/build/wsl/device}"

require_command() {
  local command_name="$1"
  if command -v "$command_name" >/dev/null 2>&1; then
    return 0
  fi

  printf 'Missing required command: %s\n' "$command_name" >&2
  printf 'Install the Linux build toolchain first, for example:\n' >&2
  printf '  sudo apt update && sudo apt install -y build-essential cmake ninja-build pkg-config git\n' >&2
  exit 1
}

print_step() {
  local message="$1"
  printf '\n==> %s\n' "$message"
}

GENERATOR_ARGS=()
if command -v ninja >/dev/null 2>&1; then
  GENERATOR_ARGS=(-G Ninja)
fi

require_command cmake
require_command ctest
require_command git
require_command python3

if command -v pkg-config >/dev/null 2>&1; then
  REQUIRED_PKGCONFIG_MODULES=(
    alsa
    wayland-client
    xkbcommon
    x11
    xrandr
    xrender
    xi
    xext
    xcursor
    xfixes
    xscrnsaver
    xtst
    egl
    gl
  )

  MISSING_MODULES=()
  for module_name in "${REQUIRED_PKGCONFIG_MODULES[@]}"; do
    if ! pkg-config --exists "$module_name"; then
      MISSING_MODULES+=("$module_name")
    fi
  done

  if (( ${#MISSING_MODULES[@]} > 0 )); then
    printf 'Missing required Linux development packages for the SDL simulator.\n' >&2
    printf 'Missing pkg-config modules: %s\n' "${MISSING_MODULES[*]}" >&2
    printf 'On Ubuntu/WSL, install the packages documented in README.md before rerunning this script.\n' >&2
    exit 1
  fi
fi

print_step "Verifying shared/platform UI boundaries"
python3 "$REPO_ROOT/scripts/check_platform_ui_boundaries.py"

print_step "Configuring Linux-shell tests into $TEST_BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$TEST_BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBUILD_TESTING=ON

print_step "Building Linux-shell tests"
cmake --build "$TEST_BUILD_DIR" --target trailmate_cardputer_zero_smoke --config "$BUILD_TYPE"
cmake --build "$TEST_BUILD_DIR" --target trailmate_cardputer_zero_runtime_smoke --config "$BUILD_TYPE"

print_step "Running Linux-shell tests"
ctest --test-dir "$TEST_BUILD_DIR" --build-config "$BUILD_TYPE" --output-on-failure

print_step "Configuring Linux simulator into $SIM_BUILD_DIR"
cmake -S "$ROOT_DIR" -B "$SIM_BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
  -DBUILD_TESTING=ON

print_step "Building Linux simulator"
cmake --build "$SIM_BUILD_DIR" --target trailmate_cardputer_zero_simulator --config "$BUILD_TYPE"

print_step "Configuring Linux framebuffer device shell into $DEVICE_BUILD_DIR"
cmake -S "$RPI_ROOT_DIR" -B "$DEVICE_BUILD_DIR" "${GENERATOR_ARGS[@]}" \
  -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

print_step "Building Linux framebuffer device shell"
cmake --build "$DEVICE_BUILD_DIR" --target trailmate_cardputer_zero_device --config "$BUILD_TYPE"

printf '\nWSL validation completed successfully.\n'
printf '  Tests build dir: %s\n' "$TEST_BUILD_DIR"
printf '  Simulator build dir: %s\n' "$SIM_BUILD_DIR"
printf '  Device build dir: %s\n' "$DEVICE_BUILD_DIR"
printf '  To launch the Linux simulator from WSL later:\n'
printf '    %s\n' "\"$SIM_BUILD_DIR/trailmate_cardputer_zero_simulator\" --scale 1"
