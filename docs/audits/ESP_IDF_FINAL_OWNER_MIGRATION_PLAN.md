# ESP-IDF Final Owner Migration Plan

This plan is a landing-zone audit only. It does not move the ESP-IDF root in
this batch, because `legacy/app_implementations/esp_idf` still carries active
component registration and target configuration.

Evidence source:

- `legacy/app_implementations/esp_idf/CMakeLists.txt`
- `legacy/app_implementations/esp_idf/idf_component.yml`
- `legacy/app_implementations/esp_idf/include/apps/esp_idf/*`
- `legacy/app_implementations/esp_idf/src/*`
- `legacy/app_implementations/esp_idf/targets/*`
- `apps/esp32_lvgl/*`
- `builds/esp_idf/*`
- current platform/module paths referenced by the ESP-IDF CMake file

## Current Active Constraint

`apps/esp32_lvgl` still links `trailmate_esp_idf_legacy_adapter` from
`legacy/app_implementations/esp_idf/src/esp_idf_legacy_implementation_adapter.cpp`.
This is allowed to remain unchanged in this batch. The adapter must not gain
new responsibilities or new callers.

## Responsibility To Final Owner Map

| Current surface | Current evidence | Final owner | Migration condition |
| --- | --- | --- | --- |
| `idf_component.yml` | `legacy/app_implementations/esp_idf/idf_component.yml` | `builds/esp_idf` or `apps/esp32_lvgl` component packaging | component registration can be invoked without using the legacy root as a semantic app root |
| `CMakeLists.txt` | `legacy/app_implementations/esp_idf/CMakeLists.txt` contains `idf_component_register` and `target_sources` | `builds/esp_idf` | IDF build wrapper owns component source lists and target selection |
| `targets/*/sdkconfig.defaults` | `legacy/app_implementations/esp_idf/targets/tab5`, `t_display_p4_tft`, `t_display_p4_amoled` | `builds/esp_idf/targets` or final board/target config owner | target defaults can move with no build-directory state reuse |
| `src/startup_runtime.cpp` and `include/apps/esp_idf/startup_runtime.h` | startup runtime calls `loop_runtime::start(config)` | `apps/esp32_lvgl` | app shell owns startup sequencing without legacy namespace |
| `src/loop_runtime.cpp` and `include/apps/esp_idf/loop_runtime.h` | loop runtime starts the IDF app loop task | `apps/esp32_lvgl` | app shell owns loop task binding |
| `src/runtime_config.cpp` and `include/apps/esp_idf/runtime_config.h` | runtime config reads `sdkconfig.h` and exposes target config | `apps/esp32_lvgl` plus `modules/product_composition` where target metadata becomes stable | target identity and runtime config are separated from build-generated SDK config |
| `src/meshtastic_radio_adapter.cpp` and `include/apps/esp_idf/meshtastic_radio_adapter.h` | ESP radio adapter source under legacy app root | `platform/esp` or protocol runtime module | radio adapter builds through platform/protocol owner without app-root include path |
| `src/gps_service_api.cpp` | includes `platform/esp/arduino_common/gps/gps_service_api.h` | GPS runtime / platform ESP GPS adapter | GPS service API is owned by GPS/platform runtime, not legacy app root |
| `src/gps_tracker_overlay.cpp` | includes `ui/screens/gps/gps_tracker_overlay.h`; CMake excludes the local copy and uses platform ESP GPS overlay | map/GPS presentation/runtime module or platform ESP UI adapter | overlay ownership is no longer duplicated in the legacy root |
| `src/team_ui_store.cpp` | includes `platform/ui/team_ui_store_runtime.h` | team presentation/runtime module or platform UI runtime | team UI store builds through stable runtime owner |
| `src/idf_entry.cpp` | defines `extern "C" void app_main(void)` | `builds/esp_idf` plus `apps/esp32_lvgl` entry hook | build entrypoint and app shell wiring are separated |
| `src/app_runtime_access.cpp` and `include/apps/esp_idf/app_runtime_access.h` | stores runtime config and app context handles | `apps/esp32_lvgl` or a stable runtime access port | app runtime access does not depend on legacy namespace |
| `src/app_facade_runtime.cpp` and `include/apps/esp_idf/app_facade_runtime.h` | large ESP app facade implementation | final app/runtime modules by responsibility | facade responsibilities are split by concrete runtime owner before deletion |
| `src/esp_idf_legacy_implementation_adapter.*` | compiled by `apps/esp32_lvgl/CMakeLists.txt` | delete after `apps/esp32_lvgl` owns historical descriptor and build migration is complete | no final app shell or CMake target compiles this adapter |

## Delete Condition For `esp_idf_legacy_implementation_adapter`

- no new calls are added;
- `apps/esp32_lvgl` uses `esp32_lvgl_historical_source_descriptor` for
  historical identity;
- build/component/source ownership no longer requires compiling
  `esp_idf_legacy_implementation_adapter.cpp`;
- active checker forbids new ESP-IDF legacy adapter callers.

## Not Done In This Batch

- no ESP-IDF component files are moved;
- no target `sdkconfig.defaults` files are moved;
- no ESP radio/GPS/team runtime code is rewritten;
- no ESP-IDF legacy root is deleted.
