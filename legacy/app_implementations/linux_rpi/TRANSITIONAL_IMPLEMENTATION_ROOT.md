# Transitional Implementation Root: linux_rpi

This directory currently contains the Pi OS / Cardputer Zero implementation
root, including the framebuffer CMake path and SDK bring-up scripts.

It is not a final app shell semantic root.

Current path:

- `legacy/app_implementations/linux_rpi`

Original historical path:

- `apps/linux_rpi`

Final app shell:

- not yet promoted to an `apps/` product shell

Authoritative build wrapper:

- not yet promoted; existing local CMake/SCons entrypoints remain transitional

Exit condition:

- a future product app shell owns the startup contract;
- build wrappers invoke that app shell;
- this implementation root can be retired or wrapped without changing device
  bring-up behavior.
