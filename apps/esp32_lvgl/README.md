# `apps/esp32_lvgl`

Role = product app shell / target app shell.

`apps/esp32_lvgl` is the future ESP + LVGL product app shell skeleton.

It is not an ESP-IDF project directory and does not contain build host files in
Phase 8.3.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

## Build Entrypoint

Build entrypoint = `builds/esp_idf`

Current transitional path = `apps/esp_idf`

## Future Responsibilities

May:

- select ESP target profile
- select board package
- select LVGL UX pack
- invoke product composition
- hand off to runtime facade

must not own:

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

This is a declaration of intent only. No behavior change in Phase 8.3.
