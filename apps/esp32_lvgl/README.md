# `apps/esp32_lvgl`

Role = product app shell / target app shell.

`apps/esp32_lvgl` is the ESP + LVGL product app shell executable baseline.

It is not an ESP-IDF project directory. Batch 2 gives this app shell final
ownership of ESP32 LVGL startup, loop, and runtime-config source.

```text
Build Entrypoint invokes.
App Shell composes.
Target chooses.
Board describes.
UX Pack presents.
```

## Build Entrypoint

Build entrypoint = `builds/esp_idf`

Historical source identity = `removed root esp_idf`

Component source list = `builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake`

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
build_entrypoint = builds/esp_idf
component_sources = builds/esp_idf/ESP_IDF_COMPONENT_SOURCES.cmake
historical_root_name = removed root esp_idf
historical_role = pre-refactor ESP-IDF/LVGL implementation root
replacement_owner = apps/esp32_lvgl + builds/esp_idf
```

Runtime owner files:

```text
src/esp32_lvgl_startup_runtime.*
src/esp32_lvgl_loop_runtime.*
src/esp32_lvgl_runtime_config.*
```

`apps/esp32_lvgl` must not compile
`esp_idf_legacy_implementation_adapter.cpp`.
