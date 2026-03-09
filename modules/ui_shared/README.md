# ui_shared

Shared LVGL UI building blocks that are reused by multiple screens and should remain portable across ESP Arduino, ESP-IDF, and future Linux LVGL targets.

## Current contents

- `ui/app_screen.h`
- `ui/app_runtime.h`
- `ui/formatters.h`
- `ui/components/two_pane_nav.h`
- `ui/components/two_pane_layout.h`
- `ui/components/two_pane_styles.h`
- `ui/screens/*` shared screen implementations
- `ui/assets/*` shared icons/fonts/images

These files are the current shared UI contracts, reusable screens, and shared UI assets that no longer belong to a single legacy `src/ui/*` subtree.

Current boundary notes:

- `ui/app_screen.h` defines the shared screen contract used by menu/runtime code
- `ui/app_runtime.h` owns shared app-switching, menu-return, focus-group, and generic LVGL menu helpers
- `ui/formatters.h` owns pure UI formatting helpers that do not depend on board state or persistence
- battery, timezone, screenshot, display glue, map tiles, and other board/product-specific UI helpers now live under `platform/esp/arduino_common`
