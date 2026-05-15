# Linux Target Profiles

Phase 8.3 target profile baseline only.

No behavior change in Phase 8.3.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

| Target | Board | Platform | Build entrypoint | App shell | UX profile | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `uconsole` | `uconsole_aio2` placeholder | Linux | `builds/linux_cmake` | `apps/linux_uconsole_gtk` | `uconsole_desktop` | transitional |
| `linux_sim` | simulator device profile | Linux | `builds/linux_cmake` | `apps/linux_sim_shell` | `simulator_full` | transitional |
| `linux_rpi_cardputer_zero` | Cardputer Zero / Pi OS device profile | Linux | `builds/linux_cmake` | future Linux device app shell | `cardputer_wide` | transitional |
| `linux_unoq` | UNO Q device profile | Linux | `builds/linux_cmake` | future Linux UNO Q app shell | `uconsole_desktop` placeholder | planned |
| `linux_headless` | host/headless profile | Linux | `builds/linux_cmake` | future Linux headless app shell | `headless_status` | planned |

## Transitional Source

Current transitional paths:

- `legacy/app_implementations/linux_uconsole`
- `legacy/app_implementations/linux_sim`
- `legacy/app_implementations/linux_rpi`
- `legacy/app_implementations/linux_unoq`

Linux CMake wrappers should invoke selected Linux app shells. They must not
absorb GTK, simulator, framebuffer, device-shell, or runtime behavior into
`builds/linux_cmake`.

## Non-Goals

This document does not move Linux CMake files, GTK source, simulator source,
Raspberry Pi source, SCons paths, or platform Linux adapters.
