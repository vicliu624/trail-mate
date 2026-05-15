# ESP32 LVGL Target Profiles

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
| `tab5` | `tab5` | ESP32-P4 | `builds/esp_idf` | `apps/esp32_lvgl` | `tab5_touch` | transitional |
| `t_display_p4` | `t_display_p4` | ESP32-P4 | `builds/esp_idf` | `apps/esp32_lvgl` | `tab5_touch` or `touch_tablet` placeholder | transitional |
| `tdeck` | `tdeck` | ESP32-S3 | `builds/esp_idf` | `apps/esp32_lvgl` | `deck_full` | planned |
| `tdeck_pro` | `tdeck_pro` | ESP32-S3 | `builds/esp_idf` | `apps/esp32_lvgl` | `deck_full` | planned |
| `tlora_pager` | `tlora_pager` | ESP32 family | `builds/esp_idf` | `apps/esp32_lvgl` | `pager_compact` | planned |
| `twatchs3` | `twatchs3` | ESP32-S3 | `builds/esp_idf` | `apps/esp32_lvgl` | `watch_quick` | planned |

## Transitional Source

Current transitional path:

- `legacy/app_implementations/esp_idf`

Current target descriptors under `legacy/app_implementations/esp_idf/targets/*` remain in place until
wrapper and app shell migration are proven.

## Non-Goals

This document does not move sdkconfig defaults, ESP-IDF CMake files, app
runtime code, or LVGL screen implementations.
