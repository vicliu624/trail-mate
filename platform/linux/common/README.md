# `platform/linux/common`

Shared Linux-safe code used by both the desktop simulator and the Pi OS device
shell.

This layer is the neutral meeting point for:

- tiny render primitives
- simulator/device-agnostic app behavior
- Linux-safe surface presentation contracts

Code should move here only when it is genuinely shared between `apps/linux_sim`
and `apps/linux_rpi`.
