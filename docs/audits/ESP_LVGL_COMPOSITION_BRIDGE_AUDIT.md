# ESP LVGL Composition Bridge Audit

## Purpose

Phase 6 moves object graph ownership toward target composition roots.

ESP LVGL startup is sensitive because it combines board power sequencing, radio
and GPS initialization, FreeRTOS task startup, LVGL display/input setup, storage
initialization, BLE setup, and existing `AppContext` binding.

This audit records the ESP LVGL composition bridge for Phase 6 without changing
startup order.

## Current Composition Surface

| Surface | Current owner | Objects or facts involved | Phase 6 policy |
| --- | --- | --- | --- |
| `apps/esp_pio/src/startup_runtime.cpp` | ESP Arduino startup | boot sequencing and runtime status | bridge, do not reorder |
| `apps/esp_pio/src/app_runtime_access.cpp` | ESP Arduino runtime access | `AppContext` bootstrap handles | bridge |
| `apps/esp_pio/src/app_context.cpp` | legacy AppContext | services, adapters, facades, config | bounded legacy bridge |
| `apps/esp_idf/src/startup_runtime.cpp` | ESP-IDF startup | boot sequencing and app facade status | bridge, do not reorder |
| `apps/esp_idf/src/app_runtime_access.cpp` | ESP-IDF runtime access | app facade runtime initialization | bridge |
| `apps/esp_idf/src/app_facade_runtime.cpp` | ESP-IDF facade runtime | minimal app facade and service wiring | bounded legacy bridge |
| `platform/esp/boards/src/board_runtime.cpp` | board runtime | board hardware handles | board descriptor / handle bridge |

## Boundary Decision

Phase 6 does not split the ESP boot chain yet.

Instead, ESP targets are treated as using a `LegacyAppContextBridge` shape:

```text
ESP startup
  -> board/runtime handle resolution
  -> legacy AppContext or app facade runtime
  -> existing services/source/sink/page bridges
  -> LVGL shell
```

This bridge is allowed only because ESP startup has target-specific lifetime and
task-ordering constraints.

## What Belongs In The Future ESP Composition Root

A future `EspLvglCompositionRoot` should own or bind:

- target identity
- board package facts
- platform adapter factories
- runtime context and task hooks
- app services
- presentation sources/sinks
- presentation models
- `PresentationWorkspace`
- LVGL shell startup/shutdown hooks

It should not interpret protocol payloads, read tile/cache content, parse GPS
sentences, or render LVGL widgets directly.

## What Stays Legacy In Phase 6

The following remain legacy-owned in this audit:

- `AppContext` service construction
- ESP app facade runtime construction
- LVGL page/controller wiring
- GPSPageState and map viewport runtime
- ChatUiController lifecycle
- board handle bootstrap ordering
- radio/GPS/BLE task startup order
- NVS/Preferences initialization order

These are not Phase 6 blockers because the ownership is now explicit and future
cleanup can move one slice at a time.

## New Code Policy

New ESP code must not:

- create app services from LVGL renderer/page code
- construct presentation source/sink/model objects inside `ui_presentation`
- bypass `ChatWorkspaceModel` for new chat UI paths
- bypass `MapWorkspaceModel` for new map workspace status/action paths
- add new global `AppContext` lookup patterns without an audit update
- put board or target product selection into renderer code

New ESP composition work should be added behind a named composition root or
behind an explicitly documented bridge.

## Phase 6 Acceptance

For Phase 6, ESP is considered composition-aware when:

- its current bridge is documented here
- `AppContext` expansion is constrained by
  `APPCONTEXT_LEGACY_BRIDGE_AUDIT.md`
- Phase 6 checkers allow the existing bridge but block new composition drift in
  pure presentation and renderer surfaces
