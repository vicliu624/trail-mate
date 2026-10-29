#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/../../.." && pwd)"
work="${TRAIL_MATE_PACKAGE_WORK:-$HOME/.cache/trailmate-cm0-package}"
out="${TRAIL_MATE_PACKAGE_OUT:-$repo/build/cm0-deb}"
jobs="${TRAIL_MATE_PACKAGE_JOBS:-4}"

bash "$script_dir/prepare_debian_sysroot.sh" "$work/sysroot" armhf
export TRAIL_MATE_SYSROOT="$work/sysroot/root"
cmake -S "$repo/apps/linux_lvgl" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$script_dir/debian-armhf.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DTRAIL_MATE_LINUX_DEVICE_CONFIG="$repo/boards/t_display_cm0/package.cmake" \
    -DTRAIL_MATE_LINUX_DEB=ON -DBUILD_TESTING=OFF
cmake --build "$work/build" --parallel "$jobs"
mkdir -p "$out"
mkdir -p "$work/packages"
# Stage on Linux storage so NTFS/DrvFS permission emulation cannot turn package
# contents into world-writable files. Only the finished archive is copied out.
cpack --config "$work/build/CPackConfig.cmake" -G DEB -B "$work/packages"
cp "$work"/packages/trailmate-cm0_*.deb "$out/"
for package in "$out"/trailmate-cm0_*.deb; do
    dpkg-deb --info "$package"
    dpkg-deb --contents "$package"
done
