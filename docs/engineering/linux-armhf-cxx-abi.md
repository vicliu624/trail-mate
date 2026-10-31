# Target-distribution C++ ABI for armhf

An armhf architecture match alone does not establish C++ standard-library ABI
compatibility. A real CM0 resource-store regression exposed heap corruption in
packages built with the generic Debian/Ubuntu armhf C++ headers and linked against
the CM0's Raspbian runtime. Native x86 tests and ordinary app startup did not expose
the mismatch.

The inspected CM0 uses GCC/libstdc++ 14.2 with
`_GLIBCXX_HAVE_ATOMIC_LOCK_POLICY` undefined. Its exported filesystem `_Dir`
shared-pointer symbols use `_Lock_policyE1` (mutex). The generic cross headers
defined atomic lock policy and selected `_Lock_policyE2`. A diagnostic build also
failed with an undefined E2 filesystem shared-pointer symbol. Using target headers
and the target link library with GCC 14 made the same resource-store contracts pass
on CM0. Do not force a library-private macro to hide this mismatch.

The target-ABI resource-store test passed on the actual device. The native x86
ASan/UBSan run also passed. The ARM sanitizer run did not complete successfully;
its runtime preload/startup failures are not sanitizer coverage for CM0.

The complete application and C++ dependencies have since been rebuilt with this
target overlay in a fresh CMake directory. CMake-built resource store and
localization activation/removal tests passed on CM0. The replacement deb was
installed, passed file/logo checks, rendered the P4 interface at 1232 x 568 and
exited through compositor-confirmed fullscreen/focused Alt+F4 with code 0 in
0.995 seconds in the current RTC-integrated build. Its executable contains no RPATH/RUNPATH to build-host directories.
See the [device ledger](t-display-cm0-hardware-integration.md) for artifact hashes.

## Preparing a target overlay

Run `apps/linux_lvgl/tools/export-target-cxx-abi.sh OUTPUT.tar.gz` on the target
distribution with its development headers installed. Transfer that archive to
the build host, then run:

```sh
python3 apps/linux_lvgl/tools/prepare-target-cxx-abi.py target-cxx.tar.gz /absolute/new/abi-overlay
```

The preparation helper requires Python 3.12 or later, accepts only the exported
header/runtime paths, refuses an existing output directory, creates the link-time
`libstdc++.so` symlink and records archive SHA-256 and ABI metadata. These are build
inputs, not application runtime dependencies copied into the deb.

Configure a **new** build directory with a compiler compatible with the exported
header version and the existing target C/OS sysroot:

```sh
export TRAIL_MATE_SYSROOT=/absolute/target-os-sysroot
export TRAIL_MATE_TARGET_CXX_ABI_ROOT=/absolute/new/abi-overlay
cmake -S apps/linux_lvgl -B build/cm0-target-abi -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/apps/linux_lvgl/tools/debian-armhf.cmake" \
  -DCMAKE_C_COMPILER=arm-linux-gnueabihf-gcc-14 \
  -DCMAKE_CXX_COMPILER=arm-linux-gnueabihf-g++-14 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
  -DTRAIL_MATE_LINUX_DEB=ON \
  -DTRAIL_MATE_LINUX_DEVICE_CONFIG="$PWD/boards/t_display_cm0/package.cmake"
```

The toolchain selects the explicit target C++ headers instead of host-distribution
headers and puts the matching target library first in the executable link search.
CM0 cross-packaging rejects a missing overlay. Native CM0 builds use their own
distribution toolchain and do not require this cross-build overlay. Other Linux
targets retain independent toolchains and target-runtime choices.

Rebuild the application and all static C++ dependencies; do not mix objects built
under different lock policies. Verify directory copy/iteration, installed-record
reopen and app resource activation on the actual target before accepting the deb.
Basic startup or a QEMU run against the generic Debian sysroot is insufficient.

## Native 1seg backend packages

The same boundary applies to the C++ GNU Radio/gr-isdbt backend; an armhf package
label does not identify its shared-pointer lock policy. The native backend
builder records the ABI from its real CMake compile command, and the package
emits `trailmate-oneseg-backend-cm0-abi-v1` only for the known CM0 profile:
libstdc++ header release 14, mutex default lock policy and C++11 ABI 1 on armhf.
The CM0 TV application requires that virtual package capability in addition to
the backend and target FFmpeg dependencies. Its packaging helper rejects an
unqualified backend before downloading/building the sysroot.

This gate prevents accepting the documented incompatible default configuration;
it is not full target ABI, RF or media acceptance. A native Raspbian backend and
the exact installed application still require the target tests above. The
2026-10-30 host package measured release 13/atomic and correctly emits no CM0
capability. An explicitly labeled empty armhf metadata fixture without the
capability was rejected in the package preflight test. No device was accessed.

## Native CM0 compiler target

The isolated Raspbian 13.6 armhf environment was subsequently bootstrapped and
measured with GCC 14.2.0-19+rpi1. Its default ARMv6 target preserves the mutex
ABI but reports bool atomics only sometimes lock-free. Selecting
`-march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard` preserves that same ABI and
provides always-lock-free bool atomics required by the FIFO cancellation path.
A compiled ARM contract was executed under QEMU; the same contract fails at
compile time under the default ARMv6 target. No private library macro was forced.

The native backend builder's `--cm0` option makes this target explicit and
records it. Full GNU Radio/backend/application builds and physical qualification
are still separate gates; the compiler probe is not media or RF acceptance.
