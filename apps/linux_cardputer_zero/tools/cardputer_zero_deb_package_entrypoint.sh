#!/usr/bin/env bash
set -euo pipefail

source_dir="${TRAIL_MATE_CARDPUTER_ZERO_CONTAINER_SOURCE:-/work/source}"
build_dir="${TRAIL_MATE_CARDPUTER_ZERO_CONTAINER_BUILD:-/work/build}"
out_dir="${TRAIL_MATE_CARDPUTER_ZERO_CONTAINER_OUT:-/work/out}"
jobs="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_JOBS:-2}"
package_version="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION:-$(python3 "${source_dir}/scripts/project_version.py" --debian)}"
package_build_type="${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_BUILD_TYPE:-Release}"
producer_dir=""
if [ -f "${source_dir}/.tmap-producer/producer-manifest.json" ]; then
    producer_dir="${source_dir}/.tmap-producer"
fi

mkdir -p "${build_dir}" "${out_dir}"
rm -f "${build_dir}"/trailmate-cardputer-zero_*.deb
rm -f "${out_dir}"/trailmate-cardputer-zero_*.deb

cmake -S "${source_dir}/builds/linux_cmake" \
    -B "${build_dir}" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE="${package_build_type}" \
    -DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" \
    -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" \
    -DBUILD_TESTING=OFF \
    -DTRAIL_MATE_BUILD_LINUX_CARDPUTER_ZERO=ON \
    -DTRAIL_MATE_BUILD_LINUX_SIM_SHELL=OFF \
    -DTRAIL_MATE_BUILD_LINUX_UCONSOLE_GTK=OFF \
    -DTRAIL_MATE_CARDPUTER_ZERO_BUILD_DEVICE=ON \
    -DTRAIL_MATE_CARDPUTER_ZERO_ENABLE_DEB_PACKAGE=ON \
    -DTRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION="${package_version}" \
    -DTRAIL_MATE_TMAP_PRODUCER_DIR="${producer_dir}"

cmake --build "${build_dir}" --target package -j "${jobs}"

mapfile -t debs < <(find "${build_dir}" -maxdepth 1 -type f -name 'trailmate-cardputer-zero_*.deb' -print)
if [ "${#debs[@]}" -ne 1 ]; then
    printf 'Expected exactly one Cardputer Zero .deb, found %s\n' "${#debs[@]}" >&2
    exit 1
fi

deb="${debs[0]}"
contents=/work/deb-contents.txt
dpkg-deb -c "${deb}" | tee "${contents}"

if awk '
    substr($1, 1, 10) == "-rwxrwxrwx" || substr($1, 1, 10) == "drwxrwxrwx" {
        print "bad package permission: " $0
        bad = 1
    }
    END { exit bad ? 1 : 0 }
' "${contents}"; then
    :
else
    printf 'Refusing to copy package with unsafe package permissions.\n' >&2
    exit 1
fi

grep -q './usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero$' "${contents}"
grep -q './usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-fbdev$' "${contents}"
grep -q './usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch$' "${contents}"
grep -q './usr/share/APPLaunch/applications/trailmate.desktop$' "${contents}"
grep -q './usr/share/APPLaunch/share/images/trailmate-cardputer-zero.png$' "${contents}"
if grep -q './usr/bin/trailmate-cardputer-zero$' "${contents}"; then
    printf 'Unexpected /usr/bin device entry found in APPLaunch package.\n' >&2
    exit 1
fi

architecture="$(dpkg-deb -f "${deb}" Architecture)"
if [ "${architecture}" != "arm64" ]; then
    printf 'Expected arm64 package, got %s\n' "${architecture}" >&2
    exit 1
fi

version="$(dpkg-deb -f "${deb}" Version)"
if [ "${version}" != "${package_version}" ]; then
    printf 'Expected package version %s, got %s\n' \
        "${package_version}" "${version}" >&2
    exit 1
fi

depends="$(dpkg-deb -f "${deb}" Depends)"
printf '%s\n' "${depends}" > /work/deb-depends.txt
for dep in libc6 libstdc++6 libgcc-s1 libsqlite3-0 libcurl4 libssl3 libwayland-client0 libxkbcommon0 libnotify-bin gdal-bin unzip ca-certificates network-manager; do
    if ! tr ', ' '\n\n' < /work/deb-depends.txt | cut -d '(' -f 1 | grep -Fxq "${dep}"; then
        printf 'Missing required package dependency: %s\nDepends: %s\n' "${dep}" "${depends}" >&2
        exit 1
    fi
    apt-cache show "${dep}" >/dev/null
done

extract=/work/extract
rm -rf "${extract}"
mkdir -p "${extract}"
dpkg-deb -x "${deb}" "${extract}"
cmp "${source_dir}/third_party/bzip2/LICENSE" \
    "${extract}/usr/share/doc/trailmate-cardputer-zero/copyright-bzip2"
binary="${extract}/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero"
fbdev_binary="${extract}/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-fbdev"
launcher="${extract}/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch"
desktop="${extract}/usr/share/APPLaunch/applications/trailmate.desktop"

test -x "${binary}"
test -x "${fbdev_binary}"
test -x "${launcher}"
sh -n "${launcher}"
grep -q '^Exec=/usr/lib/trailmate-cardputer-zero/trailmate-cardputer-zero-applaunch$' "${desktop}"
grep -q '^Icon=share/images/trailmate-cardputer-zero.png$' "${desktop}"
grep -q '^X-Zero-AppId=io.github.vicliu624.trailmate$' "${desktop}"
grep -q '^X-Zero-Display=wayland$' "${desktop}"
grep -q 'TRAIL_MATE_DISPLAY_BACKEND' "${launcher}"
grep -q 'WAYLAND_DISPLAY' "${launcher}"
grep -q 'TRAIL_MATE_RUNTIME_MODE:=mesh' "${launcher}"
grep -q 'TRAIL_MATE_LORA_SPI:=/dev/spidev0.1' "${launcher}"
grep -q 'TRAIL_MATE_LORA_RESET_GPIO:=26' "${launcher}"
grep -q 'TRAIL_MATE_LORA_BUSY_GPIO:=22' "${launcher}"
grep -q 'TRAIL_MATE_LORA_IRQ_GPIO:=23' "${launcher}"
grep -q 'TRAIL_MATE_LORA_SPI_HZ:=500000' "${launcher}"
grep -q 'TRAIL_MATE_GPS_BAUD:=115200' "${launcher}"
grep -q 'TRAIL_MATE_GPS_AUTO_SERIAL:=1' "${launcher}"
grep -q 'TRAIL_MATE_GPS_DEVICE_CANDIDATES:=/dev/serial0:/dev/ttyAMA1:/dev/ttyAMA0:/dev/ttyS0:/dev/ttyS1' "${launcher}"
! grep -q 'TRAIL_MATE_GPS_DEVICE=/dev/serial0' "${launcher}"
grep -q 'trailmate.env' "${launcher}"
grep -q 'TRAIL_MATE_EARTHDATA_TOKEN' "${launcher}"
grep -q 'TRAIL_MATE_DESKTOP_NOTIFICATIONS:=freedesktop' "${launcher}"
grep -q 'trailmate-cardputer-zero-fbdev' "${launcher}"
grep -q 'APPLAUNCH_LINUX_FBDEV_DEVICE' "${launcher}"
grep -q 'APPLAUNCH_LINUX_KEYBOARD_DEVICE' "${launcher}"

for runtime_binary in "${binary}" "${fbdev_binary}"; do
    file "${runtime_binary}" | grep -E 'aarch64|ARM aarch64|ARM64'
    readelf -h "${runtime_binary}" | grep -E 'Machine:[[:space:]]*AArch64'
    ldd "${runtime_binary}" | tee "/work/ldd-$(basename "${runtime_binary}").txt"
    if grep -q 'not found' "/work/ldd-$(basename "${runtime_binary}").txt"; then
        printf 'Runtime dependency resolution failed for %s:\n' "${runtime_binary}" >&2
        cat "/work/ldd-$(basename "${runtime_binary}").txt" >&2
        exit 1
    fi
done

cp "${deb}" "${out_dir}/"
dpkg-deb -f "${deb}" Package Version Architecture Depends
