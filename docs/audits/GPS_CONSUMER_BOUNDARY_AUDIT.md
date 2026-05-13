# GPS Consumer Boundary Audit

Phase 4.5 records the current GPS consumer boundary so Phase 5 can replace UI
surfaces with presentation models without rediscovering legacy ownership. This
audit is descriptive: it does not introduce a new GPS architecture.

## Boundary Rule

Non-UI consumers should read GPS facts through `gps::ILocationSource`,
`gps::ITimeAuthority`, or a device/status snapshot. UI consumers may continue to
use the legacy `platform/ui/gps_runtime.h` compatibility facade until the
presentation-model phase replaces those reads, but new UI work should not add new
direct `gps_runtime` dependencies.

## Consumer Inventory

| Consumer | Current source | Target source | Status | Phase owner |
| --- | --- | --- | --- | --- |
| ESP BLE PhoneCore facade | `platform/esp/arduino_common/src/ble/app_phone_facade.cpp` uses `gps::gps_location_source()` for location; epoch sync still has legacy system-time bridge code. | `gps::ILocationSource` and `gps::ITimeAuthority` via the phone facade snapshot surface. | Partial | Phase 4.5 PhoneCore facade narrowing |
| Shared Meshtastic phone runtime bridge | `platform/shared/include/platform/shared/ble/meshtastic_phone_runtime_bridge.h` converts `gps::ILocationSource` into Meshtastic GPS fix data. | Same source behind `IPhoneAppFacade` snapshot methods. | Done, but facade wiring still needs cleanup | Phase 4.5 |
| ESP Team track sampler | `platform/esp/arduino_common/src/team/runtime/team_track_source_gps.cpp` consumes `gps::ILocationSource`. | Same, with team code isolated from GPS runtime details. | Done, verify in readiness checks | Phase 4.5 |
| HostLink config/status time | `platform/esp/arduino_common/src/hostlink/hostlink_config_service.cpp` still calls `settimeofday`; status paths may compose time from legacy/system helpers. | `gps::ITimeAuthority` and `DeviceStatusSnapshot`. | Legacy | Phase 4.5/Phase 5 |
| LVGL GPS/status screens | `modules/ui_shared` screens and dashboard widgets include `platform/ui/gps_runtime.h`. The global menu/status GPS icon now reads `GpsStatusModel` through `LegacyGpsStatusSource`; dashboard GPS widgets still read the legacy runtime directly. | `GpsStatusModel` / `DeviceStatusModel` backed by `ILocationSource`, `ITimeAuthority`, and snapshots. | Partial: status icon migrated, richer GPS widgets remain legacy UI compatibility surface | Phase 5 |
| LVGL settings GPS controls | `modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp` still calls legacy GPS runtime setters for receiver, GNSS, interval, power strategy, and external NMEA settings. The `gps_enabled` toggle now submits a `SettingsModel` patch through `LegacySettingsActionSink`. | `SettingsModel` command sink backed by config patches and platform GPS runtime shell. | Partial: GPS enabled migrated, remaining GPS controls are legacy UI compatibility surface | Phase 5 / later Config Core |
| Mono 128x64 UI runtime | `modules/ui_mono_128x64/include/ui/mono_128x64/runtime.h` includes `platform/ui/gps_runtime.h`. | Device/GPS presentation snapshots. | Legacy UI compatibility surface | Phase 5 |
| GTK/uConsole dashboard, chat, map models | `platform/linux/uconsole/src/*_model.cpp` and GTK overview logic include `platform/ui/gps_runtime.h` or platform probes. | `GpsStatusModel`, `DeviceStatusModel`, and app snapshots. | Legacy UI compatibility surface | Phase 5 |
| nRF52 Meshtastic radio adapter position payload | `platform/nrf52/arduino_common/src/chat/infra/meshtastic/meshtastic_radio_adapter.cpp` includes `platform/ui/gps_runtime.h` while building position payloads near radio/protocol behavior. | `gps::ILocationSource` supplied through mesh/app projection, not direct UI runtime. | Legacy non-UI consumer | Phase 4.5 mesh cleanup |
| ESP Meshtastic adapter position payload | `platform/esp/arduino_common/src/chat/infra/meshtastic/mt_adapter.cpp` still keeps position-payload logic near legacy adapter state. | `gps::ILocationSource` supplied to core mesh or a mesh event/projection bridge. | Legacy non-UI consumer | Phase 4.5 mesh cleanup |
| ESP Arduino GPS service API | `platform/esp/arduino_common/src/gps/gps_service_api.cpp` exposes `gps_location_source()` and `gps_time_authority()` adapters over the legacy `GpsService`. | Platform GPS shell backed by `core_gps::LocationService` and `TimeAuthority`. | Compatibility shell | Later GPS shell cleanup |
| ESP-IDF GPS service API/runtime | `apps/esp_idf/src/gps_service_api.cpp` and `platform/esp/idf_common/src/gps_runtime.cpp` expose legacy runtime data and commands. | Platform GPS shell backed by `core_gps::LocationService` and `TimeAuthority`. | Compatibility shell | Later GPS shell cleanup |
| Linux simulator smoke/runtime tests | `apps/linux_sim/tests/linux_runtime_smoke.cpp` includes `platform/ui/gps_runtime.h`. | Test fixture over `ILocationSource`/presentation snapshots. | Legacy test surface | Phase 5 |

## Phase 5 Entry Constraint

Before Phase 5 creates UI presentation models, non-UI GPS reads should be either
moved to `ILocationSource`/`ITimeAuthority` or explicitly listed above as a
remaining compatibility exception. UI reads through `platform/ui/gps_runtime.h`
are accepted only as temporary inputs to the Phase 5 replacement plan.
