# ESP-IDF Runtime Source Owner Extraction Audit

This audit separates source ownership from physical historical-root cleanup.
The goal for Batch 2 is to move or assign final owners without rewriting radio,
GPS, team, or map behavior.

| Source | Current source | Final owner | Migrated now? | Deferred? | Delete condition |
| --- | --- | --- | --- | --- | --- |
| startup runtime | `legacy/app_implementations/esp_idf/src/startup_runtime.cpp` | `apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp` | yes | no | IDF component source list uses app-shell owner and no final app shell includes legacy startup headers |
| loop runtime | `legacy/app_implementations/esp_idf/src/loop_runtime.cpp` | `apps/esp32_lvgl/src/esp32_lvgl_loop_runtime.cpp` | yes | no | IDF component source list uses app-shell owner and no final app shell includes legacy loop headers |
| runtime config | `legacy/app_implementations/esp_idf/src/runtime_config.cpp` | `apps/esp32_lvgl/src/esp32_lvgl_runtime_config.cpp` plus `modules/product_composition` target metadata | yes | no | SDK config mapping and target profile lookup are owned outside the legacy root |
| Meshtastic radio adapter | `legacy/app_implementations/esp_idf/src/meshtastic_radio_adapter.cpp` | `platform/esp/radio/meshtastic_radio_adapter.cpp` | yes | no | active IDF component source list uses platform owner and app facade includes `platform/esp/radio/meshtastic_radio_adapter.h` |
| GPS service API | `legacy/app_implementations/esp_idf/src/gps_service_api.cpp` | `platform/esp/gps` or existing ESP GPS runtime owner | no | yes | stable ESP GPS owner exposes the API without legacy root source |
| GPS tracker overlay | `legacy/app_implementations/esp_idf/src/gps_tracker_overlay.cpp` | map/GPS presentation/runtime owner; current active source already comes from `platform/esp/arduino_common/src/ui/screens/gps/gps_tracker_overlay.cpp` | no | yes | duplicated legacy source is deleted after ownership report and build source list no longer references it |
| Team UI store | `legacy/app_implementations/esp_idf/src/team_ui_store.cpp` | team presentation/runtime or platform UI runtime owner; current shared header is `modules/core_sys/include/platform/ui/team_ui_store_runtime.h` | no | yes | stable team runtime source owns UI store behavior outside the historical root |
| app runtime access | `legacy/app_implementations/esp_idf/src/app_runtime_access.cpp` | `apps/esp32_lvgl` or stable runtime access port | no | yes | app runtime access no longer depends on `apps::esp_idf` namespace |
| app facade runtime | `legacy/app_implementations/esp_idf/src/app_facade_runtime.cpp` | final app/runtime modules by concrete responsibility | partially | yes | facade responsibilities are split by runtime owner before historical root deletion |

Batch 2 extraction result:

- active radio adapter owner is `platform/esp/radio`;
- active startup, loop, and runtime config owners are `apps/esp32_lvgl`;
- target identity metadata starts in `modules/product_composition`;
- remaining GPS/team/map/app-facade surfaces are deferred with explicit owners.
