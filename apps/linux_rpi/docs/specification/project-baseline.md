# Trail Mate Cardputer Zero Linux Baseline

This document describes the Linux baseline and development stance.
The authoritative final-shape constraint document is
[cardputer-zero-adaptation-spec.md](./cardputer-zero-adaptation-spec.md).

## 1. Current Confusions

- `Cardputer Zero` is a Linux handheld running Pi OS, so it should not be modeled
  as another MCU firmware board bring-up.
- `M5Stack_Linux_Libs` and `M5CardputerZero-UserDemo` do not play the same role:
  the former is a reusable device SDK, while the latter is only a loose demo.
- The current Trail Mate repository already has the target directory names for
  Linux, but the existing shared modules are not yet portable enough to be
  reused directly.
- The repository root `CMakeLists.txt` is still owned by the ESP-IDF path, so a
  Linux baseline should not try to hijack the root build.
- A simulator is required for fast iteration on Windows and WSL, but the
  simulator must not become the device runtime.
- A Linux dev container is useful for workflow parity with Pi-OS-like userland,
  but it is not a substitute for real framebuffer validation on the device.

## 2. Candidate Distinctions

- `apps/linux_sim`
  The simulator and developer-tooling shell.
- `apps/linux_rpi`
  The real Pi OS hardware shell.
- `M5Stack_Linux_Libs`
  The device-facing SDK baseline for Cardputer Zero display, input, LVGL, and
  low-level Linux bus access.
- `M5CardputerZero-UserDemo`
  Reference material only for board-specific hints, not a project template.
- `platform/linux/common`
  The Linux-safe shared slice used by both shells.
- `platform/linux/rpi`
  Pi OS specific platform adapters that remain valuable only where the vendor
  SDK does not already provide the right primitive.

## 3. Invalid Distinctions

- Do not force current Arduino or ESP-IDF runtime glue into this shell just to
  make it compile.
- Do not treat `UserDemo` as a rigorous Linux engineering baseline just because
  it runs on the target hardware.
- Do not treat SDL types, framebuffer ioctls, or Linux device paths as shared
  app concepts.
- Do not pretend the current `modules/ui_shared` tree is already Linux-safe when
  it still depends on ESP/Arduino pieces.
- Do not use this baseline as an excuse to create a second long-term copy of the
  full Trail Mate business logic.
- Do not let `apps/linux_rpi` depend on simulator-only code living inside
  `apps/linux_sim`.

## 4. Engineer Decisions Already Made

- Language: `C++20`
- Desktop simulator build system: standalone subproject `CMake`
- Device-facing SDK build system: `M5Stack_Linux_Libs` `SCons` + `Kconfig`
- Desktop simulator backend: `SDL3`
- Device SDK baseline: `M5Stack_Linux_Libs`
- Device reference only: `M5CardputerZero-UserDemo`
- Device baseline: Linux framebuffer output through the SDK-backed LVGL path
- Linux dev container baseline: Debian `Trixie`
- Root build isolation: the Linux shells stay under `apps/linux_sim` and
  `apps/linux_rpi` and do not replace the repository-root ESP-IDF build
- Shared Linux slice: `platform/linux/common`

## 5. Current Baseline

- The simulator must build on both Windows and Linux.
- The framebuffer device target must build on Linux and be usable on Pi OS.
- The real device adaptation should prefer the vendor Linux SDK over custom
  framebuffer code whenever the SDK already owns the problem well.
- The baseline app must stay tiny and intentionally generic.
- The Linux-safe shared slice belongs in `platform/linux/common`.
- Final Cardputer Zero feature work should migrate reusable logic out of this
  shell split into the main repository structure instead of letting either shell
  become a permanent fork.
- Feature-first execution is not allowed to redefine the architecture; feature
  slices must validate the final-shape specification instead.

## 6. Minimal Externalization

- Use the simulator shell to prove iteration speed, and use the Pi shell to
  prove real-device viability.
- Use `M5Stack_Linux_Libs` for LVGL, fbdev, evdev, and low-level device-facing
  bring-up instead of promoting those responsibilities into Trail Mate too
  early.
- Promote code into `platform/linux/common` only after a responsibility is
  clearly proven to be shared.
- Keep device-specific Linux details in `platform/linux/rpi`.
- Keep simulator geometry and desktop input mapping in `apps/linux_sim`.

## 7. Recommended Next Migration Slices

1. Define Linux-safe input/runtime contracts that can outlive this shell.
2. Extract presentation-independent view state and formatting helpers.
3. Extract storage and filesystem abstractions.
4. Move Linux-safe pieces of chat/GPS/team logic only after their side effects
   are clearly separated from ESP runtime services.
5. Grow `apps/linux_rpi` only with real device concerns, not with duplicated
   desktop tooling or copied demo-app structure.
