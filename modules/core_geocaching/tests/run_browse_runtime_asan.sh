#!/bin/sh
# Linux host check for the actual device Session with hardware-only seams.
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$repo_dir"
build_dir=.codex-build/geocaching-runtime-asan
crypto_dir=modules/core_chat/src/infra/meshcore/crypto/ed25519
mkdir -p "$build_dir"
# Keep the sanitizer run on the same production maintenance adapter as CMake.
# Fail explicitly if its extraction boundaries change.
python3 - "$build_dir/runtime_storage_adapter.inc" <<'PY'
from pathlib import Path
import sys

source = Path("platform/esp/arduino_common/src/storage/storage_runtime.cpp").read_text()
start = source.index("class SdMaintenanceAdapter final")
end = source.index("\nSdMaintenanceAdapter s_adapter", start)
Path(sys.argv[1]).write_text(source[start:end])
PY
for part in verify sign keypair fe ge sc sha512; do
    gcc -g -O1 -fsanitize=address -fno-omit-frame-pointer -c "$crypto_dir/$part.c" -o "$build_dir/$part.o"
done
g++ -std=c++17 -g -O1 -fsanitize=address -fno-omit-frame-pointer -DTRAIL_MATE_RETICULUM_HASH_ONLY=1 \
    -I modules/core_geocaching/tests/runtime_fakes -I modules/core_geocaching/tests/fakes \
    -I modules/core_geocaching/include -I modules/core_sys/include -I modules/core_gps/include \
    -I modules/core_chat/include -I modules/ui_presentation/include -I platform/esp/arduino_common/include \
    -I platform/esp/common/include -I "$build_dir" \
    modules/core_geocaching/tests/test_browse_runtime.cpp \
    modules/core_geocaching/tests/runtime_fakes/sd_runtime.cpp \
    platform/esp/arduino_common/src/geocaching/browse_runtime.cpp \
    platform/esp/arduino_common/src/geocaching/request_dispatcher.cpp \
    modules/core_chat/src/infra/reticulum/reticulum_wire.cpp \
    "$build_dir/verify.o" "$build_dir/sign.o" "$build_dir/keypair.o" "$build_dir/fe.o" \
    "$build_dir/ge.o" "$build_dir/sc.o" "$build_dir/sha512.o" \
    -o "$build_dir/browse_runtime"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 "$build_dir/browse_runtime" modules/core_geocaching/tests/fixtures
