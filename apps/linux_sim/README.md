# Trail Mate Cardputer Zero Simulator

`apps/linux_sim` is now the simulator and developer-tooling shell for the future
`Cardputer Zero` adaptation.

This directory intentionally preserves the `LoFiBox-Zero` workflow model for
desktop development:

- a self-contained `CMake` workspace
- a Windows/Linux `SDL3` desktop simulator for fast iteration
- scripted build/run flows for Windows, Linux, WSL, and a Linux GUI dev container
- smoke tests for the extracted Linux-safe shared slice

The real Pi OS device shell now lives in
[`apps/linux_rpi`](../linux_rpi/README.md).

## What stays here

- simulator entrypoints and shell rendering
- Windows build ergonomics
- Linux desktop build ergonomics
- WSL validation for the Linux shell
- Docker-based Linux GUI development
- smoke tests for the Linux-safe shared slice

## What moved out

- Pi OS framebuffer device target
- Cardputer Zero device-specific documentation
- device build instructions and device entrypoint

Those now belong to [`apps/linux_rpi`](../linux_rpi/README.md), while the shared
Linux-safe code they both use now lives under
[`platform/linux/common`](../../platform/linux/common/README.md).

## Layout

- `CMakeLists.txt`: standalone simulator shell build
- `CMakePresets.json`: Windows/Linux presets for simulator and tests
- `src/platform/simulator`: SDL front-view device simulator
- `src/targets`: simulator entrypoint
- `tests`: smoke tests for the Linux-safe shared slice
- `scripts`: helper scripts for build/run/dev-container flows
- `docker`: Linux GUI dev-container assets

## Quick Start

### Windows simulator

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-simulator.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\run-simulator.ps1
```

### Windows Visual Studio solution

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\generate-windows-solution.ps1
powershell -ExecutionPolicy Bypass -File .\scripts\open-windows-solution.ps1
```

### Linux simulator

```bash
bash scripts/build-simulator.sh
bash scripts/run-simulator.sh
```

### WSL validation

```bash
bash scripts/wsl-validate.sh
```

### Linux GUI dev container

From WSL:

```bash
bash scripts/run-dev-container.sh
```

From Windows PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\run-dev-container.ps1
```

## Boundary note

This shell is intentionally simulator-first. It should not become the place
where real Pi OS hardware concerns accumulate. Device-facing code belongs in
`apps/linux_rpi` and `platform/linux/rpi`, while only the truly shared Linux-safe
slice should flow into `platform/linux/common`.
