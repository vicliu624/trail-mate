# App Shell Manifest: esp32_lvgl

## Role

Product app shell / target app shell.

## Target Family

ESP / ESP32-P4.

## Renderer Family

LVGL.

## Build Entrypoint

Future authoritative build entrypoint:

- `builds/esp_idf`

Current transitional path:

- `apps/esp_idf`

## Future Responsibilities

May:

- select ESP target profile
- select board package
- select LVGL UX pack
- invoke product composition
- hand off to runtime facade

Must not:

- own build host files
- own ESP-IDF CMake project files
- define board facts
- implement HAL details
- implement screen internals
- assemble Chat/Map/GPS runtime directly in build wrapper

## Thin App Shell Entrypoint Declaration

Future declaration:

```text
trail_mate_esp32_lvgl_start(target_profile)
```

## Current Status

Skeleton only.

No behavior change in Phase 8.3.
