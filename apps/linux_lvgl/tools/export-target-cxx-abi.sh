#!/bin/sh
# Run on the target distribution. Export only its installed C++ headers/runtime.
set -eu
test "$#" -eq 1 || { echo "Usage: $0 OUTPUT.tar.gz" >&2; exit 2; }
compiler=${CXX:-g++}
major=$("$compiler" -dumpversion | cut -d . -f 1)
multiarch=$("$compiler" -print-multiarch)
library=$(readlink -f "$("$compiler" -print-file-name=libstdc++.so)")
case "$library" in
    /usr/lib/"$multiarch"/libstdc++.so.*) ;;
    *) echo "Unexpected target C++ runtime location: $library" >&2; exit 1 ;;
esac
test -f "/usr/include/$multiarch/c++/$major/bits/c++config.h"
tar -czf "$1" -C / "usr/include/c++/$major" \
    "usr/include/$multiarch/c++/$major" "${library#/}"
sha256sum "$1"
