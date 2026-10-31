#!/usr/bin/env bash
set -euo pipefail
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$script_dir/../../.." && pwd)"
work="${TRAIL_MATE_PACKAGE_WORK:-$HOME/.cache/trailmate-cm0-package}"
out="${TRAIL_MATE_PACKAGE_OUT:-$repo/build/cm0-deb}"
jobs="${TRAIL_MATE_PACKAGE_JOBS:-4}"
oneseg="${TRAIL_MATE_PACKAGE_ONESEG:-0}"
backend="${TRAIL_MATE_ONESEG_BACKEND_DEB:-}"
case "$oneseg" in
    0) media=OFF; sysroot_feature=base ;;
    1)
        media=ON; sysroot_feature=oneseg
        if [ ! -f "$backend" ]; then
            echo "1seg packaging requires TRAIL_MATE_ONESEG_BACKEND_DEB from a matching native CM0 build" >&2
            exit 1
        fi
        if [ "$(dpkg-deb -f "$backend" Package)" != trailmate-oneseg-backend ] ||
           [ "$(dpkg-deb -f "$backend" Architecture)" != armhf ]; then
            echo "CM0 1seg requires a trailmate-oneseg-backend armhf package" >&2
            exit 1
        fi
        provides="$(dpkg-deb -f "$backend" Provides)"
        if [[ ",${provides//[[:space:]]/}," != *",trailmate-oneseg-backend-cm0-abi-v1,"* ]]; then
            echo "CM0 1seg backend does not provide the required Raspbian C++ ABI" >&2
            exit 1
        fi
        if [[ ",${provides//[[:space:]]/}," != *",trailmate-oneseg-backend-auto-v1,"* ]] ||
           ! dpkg --compare-versions "$(dpkg-deb -f "$backend" Version)" ge 1.1.0; then
            echo "CM0 1seg backend requires version >= 1.1.0 and automatic TMCC acquisition capability" >&2
            exit 1
        fi
        ;;
    *) echo "TRAIL_MATE_PACKAGE_ONESEG must be 0 or 1" >&2; exit 1 ;;
esac

bash "$script_dir/prepare_debian_sysroot.sh" "$work/sysroot" armhf "$sysroot_feature"
export TRAIL_MATE_SYSROOT="$work/sysroot/root"
cmake -S "$repo/apps/linux_lvgl" -B "$work/build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$script_dir/debian-armhf.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
    -DTRAIL_MATE_LINUX_DEVICE_CONFIG="$repo/boards/t_display_cm0/package.cmake" \
    -DTRAIL_MATE_LINUX_ENABLE_ONESEG_MEDIA="$media" \
    -DTRAIL_MATE_LINUX_DEB=ON -DBUILD_TESTING=OFF
cmake --build "$work/build" --parallel "$jobs"
mkdir -p "$out"
mkdir -p "$work/packages"
# Stage on Linux storage so NTFS/DrvFS permission emulation cannot turn package
# contents into world-writable files. Only the finished archive is copied out.
cpack --config "$work/build/CPackConfig.cmake" -G DEB -B "$work/packages"
cp "$work"/packages/trailmate-cm0_*.deb "$out/"
if [ "$oneseg" = 1 ]; then
    cp "$backend" "$out/"
fi
for package in "$out"/trailmate-cm0_*.deb; do
    dpkg-deb --info "$package"
    dpkg-deb --contents "$package"
done
