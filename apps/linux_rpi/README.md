# Trail Mate Cardputer Zero Pi OS Device Shell

`apps/linux_rpi` is the real Pi OS device shell for `Cardputer Zero`.

This directory now has two responsibilities that should stay distinct:

- a repo-local thin Linux bring-up shell for neutral experiments
- an SDK-backed device shell that uses `M5Stack_Linux_Libs` for real Cardputer
  Zero display and input bring-up

The desktop simulator, Windows tooling, WSL validation, and dev-container
workflow remain in [`apps/linux_sim`](../linux_sim/README.md).

## Layout

- `CMakeLists.txt`: standalone Pi OS device shell build
- `CMakePresets.json`: Linux preset for the framebuffer device target
- `SConstruct`: `M5Stack_Linux_Libs` backed device build entrypoint
- `config_defaults.mk`: default SDK configuration for LVGL 9.5 + fbdev + evdev
- `main`: clean SDK project component for the real device shell
- `docs/specification`: current Cardputer Zero boundaries and migration notes
- `src/targets`: thin device entrypoint
- `scripts`: Linux helper scripts for build and run

The shared Linux-safe slice used by both `linux_sim` and `linux_rpi` now lives
in [`platform/linux/common`](../../platform/linux/common/README.md).

## Reference Rule

- `M5Stack_Linux_Libs` is the device-facing SDK baseline we should actually
  build on.
- `M5CardputerZero-UserDemo` is reference material only. It can provide board
  hints such as `fb_st7789v` detection and likely evdev paths, but it is not
  the engineering template for this repository.
- New Cardputer Zero work should prefer the SDK-backed path over extending the
  custom framebuffer shell when the work is truly device-facing.

## Quick Start

### Pi OS / Linux device build

```bash
bash scripts/build-device.sh
```

### Pi OS / Linux device run

```bash
TRAIL_MATE_FBDEV=/dev/fb0 bash scripts/run-device.sh
```

### M5Stack Linux SDK build

Set `TRAIL_MATE_M5STACK_LINUX_LIBS_PATH` to your `M5Stack_Linux_Libs`
checkout, or keep a local checkout at `.tmp/M5Stack_Linux_Libs`.

```bash
bash scripts/build-sdk-device.sh
```

### M5Stack Linux SDK run

```bash
LV_LINUX_FBDEV_DEVICE=/dev/fb0 bash scripts/run-sdk-device.sh
```

### CMake preset flow

```bash
cmake --preset linux-device-debug
cmake --build --preset linux-device-debug-build
TRAIL_MATE_FBDEV=/dev/fb0 ./build/device/trailmate_cardputer_zero_device
```

## Notes

- This shell is intentionally thin. It should own device lifecycle and device
  adapters, not duplicate the simulator tooling.
- The `SConstruct` path is the preferred direction for real `Cardputer Zero`
  hardware adaptation because it rides on the vendor Linux SDK instead of
  re-solving LVGL and device-driver plumbing from scratch.
- For iteration on UI behavior, prefer the simulator in `apps/linux_sim`.
- For actual hardware bring-up on Pi OS, build and run from here.
