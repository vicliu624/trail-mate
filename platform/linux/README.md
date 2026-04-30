# `platform/linux`

Linux-specific platform implementations.

This layer now holds the shared Linux-safe slice for Cardputer Zero, plus
Pi-specific platform adapters.

- `platform/linux/common`: code that is safe to share between simulator and
  Pi OS device shells
- `platform/linux/rpi`: Pi OS / framebuffer specific platform adapters

Keep simulator-only presentation and desktop tooling in `apps/linux_sim`, and
keep device shell ownership in `apps/linux_rpi`.

For real Cardputer Zero hardware bring-up, prefer `M5Stack_Linux_Libs` for the
device-facing LVGL and driver path. Treat `platform/linux/rpi` as the place for
Trail Mate specific Linux adapters that remain necessary after the SDK takes
care of the generic hardware plumbing.
