# `apps/esp_pio`

PlatformIO application shell for the current Arduino-based ESP targets.

Current responsibilities:

- own the Arduino `setup/loop` implementation plus startup composition for the active PlatformIO shell
- own the thin PlatformIO/Arduino loop + startup adapters that assemble board/runtime hooks
- own the current shared app-catalog composition for the Arduino/PlatformIO product shell
- own only PlatformIO-side startup / loop / runtime composition, not per-page wrapper implementations
- consume shared `modules/*`
- consume `platform/esp/*` runtime adapters

Current status:

- now wired into the active PlatformIO build through the thin `src/main.cpp` trampoline, with the real Arduino entry living in `apps/esp_pio/src/arduino_entry.cpp` plus startup and loop runtime
- now treats the menu shell/profile/runtime, boot overlay, watch face, status shell, and startup/loop skeleton as shared `modules/ui_shared` code
- now mainly owns PlatformIO-specific composition: board bring-up order, Arduino loop hooks, runtime bootstrap, and app-catalog feature gating
- no longer keeps per-page wrapper files under `apps/esp_pio`; menu entries resolve into shared `modules/ui_shared` page shells
- the authoritative PlatformIO build entry remains root `platformio.ini`, but the app-shell/page boundary is now settled
