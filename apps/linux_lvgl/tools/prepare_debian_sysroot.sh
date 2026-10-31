#!/usr/bin/env bash
set -euo pipefail

# Download and unpack a Debian development sysroot without changing the host's
# packages. This directory must be dedicated to this script (no existing OS).
work="${1:?usage: prepare_debian_sysroot.sh WORK_DIRECTORY [armhf|arm64]}"
arch="${2:-armhf}"
feature="${3:-base}"
extra_packages=()
case "$feature" in
    base) ;;
    oneseg) extra_packages=(libavformat-dev libavcodec-dev libavutil-dev libswresample-dev libswscale-dev libaribb24-dev) ;;
    *) echo "Unsupported sysroot feature: $feature" >&2; exit 1 ;;
esac
case "$arch" in armhf|arm64) ;; *) echo "Unsupported architecture: $arch" >&2; exit 1 ;; esac
mkdir -p "$work"
work="$(cd "$work" && pwd)"
mkdir -p "$work/apt/lists/partial" "$work/apt/archives/partial" "$work/root"
touch "$work/apt/status"
if [ ! -f "$work/apt/debian-trixie.gpg" ]; then
    curl --fail --location --silent --show-error \
        https://ftp-master.debian.org/keys/archive-key-13.asc \
        -o "$work/apt/archive-key-13.asc"
    gpg --dearmor < "$work/apt/archive-key-13.asc" > "$work/apt/debian-trixie.gpg"
fi
printf 'deb [arch=%s signed-by=%s] https://deb.debian.org/debian trixie main\n' \
    "$arch" "$work/apt/debian-trixie.gpg" > "$work/apt/sources.list"
options=(-o "APT::Architecture=$arch"
    -o "Dir::Etc::sourcelist=$work/apt/sources.list"
    -o "Dir::Etc::sourceparts=-"
    -o "Dir::State::status=$work/apt/status"
    -o "Dir::State::lists=$work/apt/lists"
    -o "Dir::Cache::archives=$work/apt/archives"
    -o "Dir::Cache::pkgcache=$work/apt/pkgcache.bin"
    -o "Dir::Cache::srcpkgcache=$work/apt/srcpkgcache.bin"
    -o "APT::Get::List-Cleanup=0"
    -o "APT::Sandbox::User=$(id -un)")
apt-get "${options[@]}" update
apt-get "${options[@]}" --download-only --no-install-recommends --assume-yes install \
    libc6-dev libstdc++-14-dev libsdl2-dev libcurl4-openssl-dev libsqlite3-dev libssl-dev "${extra_packages[@]}"
for package in "$work"/apt/archives/*.deb; do
    dpkg-deb --extract "$package" "$work/root"
done
# Debian Trixie uses merged-/usr; archive extraction does not run base-files'
# maintainer scripts that normally create these compatibility links.
for directory in lib bin sbin; do
    if [ ! -e "$work/root/$directory" ]; then
        ln -s "usr/$directory" "$work/root/$directory"
    fi
done
printf '\nSysroot ready: %s/root\n' "$work"
