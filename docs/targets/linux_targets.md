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
| `cardputerzero` | Cardputer Zero portable Linux device profile | Linux | `builds/linux_cmake` | `apps/linux_cardputer_zero` | `cardputer_compact` | pending hardware validation |
| `t_display_cm0` | `boards/t_display_cm0/device.conf` | Linux | `builds/linux_cmake` | `apps/linux_lvgl` | `t_display_p4_touch` | pending hardware validation |
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

`cardputerzero` is no longer described as a future Raspberry Pi simulator path.
Its current owner is `apps/linux_cardputer_zero`; the remaining work is real
Cardputer Zero Wayland APPLaunch startup, keyboard mapping, per-page 320 x 170
screenshots from the Pager-derived UI path, notification daemon session,
Fcitx5 session, explicit framebuffer/evdev debug fallback, and package
validation.

## Non-Goals

This document does not move Linux CMake files, GTK source, simulator source,
Raspberry Pi source, SCons paths, or platform Linux adapters.
