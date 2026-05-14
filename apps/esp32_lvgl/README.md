# `apps/esp32_lvgl`

Role = product app shell / target app shell.

`apps/esp32_lvgl` is the ESP + LVGL product app shell executable baseline.

It is not an ESP-IDF project directory and does not contain build host files in
Phase 8 Build/AppShell Executable Convergence.

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

Current source-level shell descriptor:

```text
src/esp32_lvgl_app_shell.h
src/esp32_lvgl_app_shell.cpp
trailmate_esp32_lvgl_app_shell
```

Current config:

```text
target_family = esp32_lvgl
default_ux_pack_id = compatibility
transitional_source = apps/esp_idf
legacy_adapter_target = trailmate_esp_idf_legacy_adapter
```

No behavior change in Phase 8 Build/AppShell Executable Convergence.
