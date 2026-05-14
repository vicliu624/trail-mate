# Transitional Implementation Root: esp_idf

This directory currently contains the physical ESP-IDF component and build
integration.

It is not the final app shell semantic root.

Final app shell:

- `apps/esp32_lvgl`

Authoritative build wrapper:

- `builds/esp_idf`

Current status:

- transitional implementation root
- transitional build root for the physical ESP-IDF component
- retained to avoid moving sdkconfig, IDF component registration, or target
  CMake assumptions during Phase 8 Build/AppShell Executable Convergence

Exit condition:

- `builds/esp_idf` invokes `apps/esp32_lvgl`;
- `apps/esp32_lvgl` owns the app shell startup contract;
- `apps/esp_idf` remains only a legacy adapter / physical IDF component root;
- target-specific sdkconfig and CMake integration can move without behavior
  change.
