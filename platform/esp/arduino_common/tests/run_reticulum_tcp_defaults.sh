#!/usr/bin/env bash
set -euo pipefail
# Linux host-only regression. Pass the directory containing ESP-IDF's cJSON.h.
root="$(cd "$(dirname "$0")/../../../.." && pwd)"
json_include="${1:?Pass the directory containing cJSON.h}"
output="${2:-$root/.codex-build/reticulum-tcp-defaults-test}"
cd "$root"
"${CXX:-g++}" -std=c++17 -ffunction-sections -fdata-sections -Wl,--gc-sections \
  -Imodules/core_sys/include -Imodules/core_chat/include \
  -Iplatform/esp/arduino_common/include -I"$json_include" \
  platform/esp/arduino_common/tests/test_reticulum_tcp_defaults.cpp \
  platform/esp/arduino_common/src/platform_ui_reticulum_network_config_runtime.cpp \
  -o "$output"
"$output"
