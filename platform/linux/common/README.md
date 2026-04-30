# `platform/linux/common`

Shared Linux-safe code used by both the desktop simulator and the Pi OS device
shell.

This layer is the neutral meeting point for:

- tiny render primitives
- simulator/device-agnostic app behavior
- Linux-safe surface presentation contracts
- Linux implementations of shared `platform::ui::*` runtime contracts such as
  settings, time, screen timeout, and generic device state
- the shared Linux boot/menu shell session used by both the simulator and the
  thin framebuffer device shell

Code should move here only when it is genuinely shared between `apps/linux_sim`
and `apps/linux_rpi`.
