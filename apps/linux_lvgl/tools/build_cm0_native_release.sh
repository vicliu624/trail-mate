#!/usr/bin/env bash
set -euo pipefail

# CM0 uses Raspbian's GCC 14 mutex ABI, not generic Debian's ARMHF ABI.
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
work="${TRAIL_MATE_CM0_RELEASE_WORK:-/tmp/trailmate-cm0-release}"
root="${TRAIL_MATE_CM0_ROOTFS:-$work/rootfs}"
jobs="${TRAIL_MATE_PACKAGE_JOBS:-4}"
producer="${TRAIL_MATE_TMAP_PRODUCER_DIR:?A published linux-arm TMAP producer is required}"
test "$(id -u)" = 0
test "$root" != / && test "$root" != "$repo"
test -f "$producer/producer-manifest.json"
mkdir -p "$work" "$repo/build/cm0-deb"

if [ "${1:-}" != --namespace ]; then
    if [ ! -f "$root/etc/os-release" ]; then
        curl --fail --location --retry 3 https://archive.raspbian.org/raspbian.public.key -o "$work/raspbian.key"
        fingerprint=$(gpg --show-keys --with-colons "$work/raspbian.key" | awk -F: '$1=="fpr" {print $10; exit}')
        test "$fingerprint" = A0DA38D0D76E8B5D638872819165938D90FDDD2E
        gpg --batch --yes --dearmor -o "$work/raspbian.gpg" "$work/raspbian.key"
        debootstrap --arch=armhf --variant=minbase --foreign --include=raspbian-archive-keyring,ca-certificates \
            --keyring="$work/raspbian.gpg" trixie "$root" https://archive.raspbian.org/raspbian
        chroot "$root" /debootstrap/debootstrap --second-stage
    fi
    exec unshare --mount --pid --fork --propagation private bash "$0" --namespace
fi

grep -q '^ID=raspbian$' "$root/etc/os-release"
test "$(chroot "$root" dpkg --print-architecture)" = armhf
mkdir -p "$root/proc" "$root/workspace" "$root/tmap-producer"
mount -t proc proc "$root/proc"
mount --bind "$repo" "$root/workspace"
mount -o remount,bind,ro "$root/workspace"
mount --bind "$producer" "$root/tmap-producer"
mount -o remount,bind,ro "$root/tmap-producer"
trap 'umount "$root/tmap-producer"; umount "$root/workspace"; umount "$root/proc"' EXIT
printf '#!/bin/sh\nexit 101\n' > "$root/usr/sbin/policy-rc.d"
chmod 755 "$root/usr/sbin/policy-rc.d"
printf 'deb https://archive.raspbian.org/raspbian trixie main\n' > "$root/etc/apt/sources.list"
chroot "$root" /usr/bin/env DEBIAN_FRONTEND=noninteractive /bin/bash -eu -c '
    apt-get -o APT::Update::Error-Mode=any update
    apt-get install -y --no-install-recommends ca-certificates build-essential g++-14 cmake ninja-build pkg-config dpkg-dev git curl python3 python3-dev python3-numpy pybind11-dev gnuradio gnuradio-dev libgsl-dev libsimde-dev libsdl2-dev libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libaribb24-dev ffmpeg libcurl4-openssl-dev libarchive-dev libsqlite3-dev libssl-dev libjpeg-dev libpng-dev libbluetooth-dev libicu76
'
chroot "$root" /usr/bin/env -i PATH=/usr/sbin:/usr/bin:/sbin:/bin HOME=/root LANG=C.UTF-8 \
    CC=gcc-14 CXX=g++-14 JOBS="$jobs" /bin/bash -eu -c '
    python3 /workspace/apps/linux_lvgl/tools/build_oneseg_backend.py --cm0 --work-dir /build/gr-isdbt --jobs "$JOBS"
    python3 /workspace/apps/linux_lvgl/tools/package_oneseg_backend.py --backend-work /build/gr-isdbt --output /build/release-packages
    cmake -S /workspace/apps/linux_lvgl -B /build/release-application -G Ninja \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
        "-DCMAKE_C_FLAGS=-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard" \
        "-DCMAKE_CXX_FLAGS=-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard" \
        -DTRAIL_MATE_LINUX_DEVICE_CONFIG=/workspace/boards/t_display_cm0/package.cmake \
        -DTRAIL_MATE_LINUX_ENABLE_ONESEG_MEDIA=ON -DTRAIL_MATE_LINUX_DEB=ON -DBUILD_TESTING=OFF \
        -DTRAIL_MATE_TMAP_PRODUCER_DIR=/tmap-producer \
        -DTRAIL_MATE_ONESEG_GENERATE_BROADCAST_FONT=OFF \
        -DTRAIL_MATE_ONESEG_BROADCAST_FONT_FILE=/workspace/packs/ja-broadcast/fonts/ja-broadcast-cjk/font.bin
    cmake --build /build/release-application --parallel "$JOBS"
    cpack --config /build/release-application/CPackConfig.cmake -G DEB -B /build/release-packages
'
cp "$root"/build/release-packages/*.deb "$repo/build/cm0-deb/"
