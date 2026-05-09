#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd -- "${script_dir}/.." && pwd)"
binary="${project_dir}/dist/trailmate_cardputer_zero_m5sdk"

if [[ ! -x "${binary}" ]]; then
    echo "SDK device binary not found at ${binary}. Build it first with scripts/build-sdk-device.sh." >&2
    exit 1
fi

if [[ -z "${LV_LINUX_FBDEV_DEVICE:-}" ]]; then
    export LV_LINUX_FBDEV_DEVICE=/dev/fb0
fi

exec "${binary}" "$@"
