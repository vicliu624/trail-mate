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

Historical source identity:

- `removed root esp_idf`

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

Current source-level shell descriptor:

```text
src/esp32_lvgl_app_shell.h
src/esp32_lvgl_app_shell.cpp
trailmate_esp32_lvgl_app_shell
```

## Current Status

Executable app shell baseline.

Default UX Pack:

- `compatibility`

UX Pack Runtime Binding:

- `Esp32LvglAppShell::validate()` resolves `default_ux_pack_id` through
  `findUxPackById`.

Current ESP-IDF final owner surfaces:

- `esp32_lvgl_startup_runtime.*`
- `esp32_lvgl_loop_runtime.*`
- `esp32_lvgl_runtime_config.*`

Component source ownership:

- `builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake`

Forbidden active dependency:

- `esp_idf_legacy_implementation_adapter.cpp`
