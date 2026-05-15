# ESP-IDF Build Entrypoint

Authoritative ESP build entrypoint.

Build authority:

```text
ESP / ESP32-P4 -> ESP-IDF
```

Current transitional path:

```text
legacy/app_implementations/esp_idf
```

Final wrapper direction:

```text
builds/esp_idf
  -> invokes apps/esp32_lvgl app shell
```

This directory is a Phase 8.2 migration stub. It does not yet contain ESP-IDF
CMake project files, component manifests, sdkconfig defaults, or generated build
state.

Rules:

- thin wrapper only
- Build Entrypoint invokes
- App Shell composes
- do not assemble Chat/Map/GPS runtime here
- do not choose UX pack here
- do not define board facts here
- do not move current ESP-IDF files until equivalent wrapper invocation is
  validated
