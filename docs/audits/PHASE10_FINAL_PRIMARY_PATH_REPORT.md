# Phase 10 Final Primary Path Report

## Primary Path Status

| Runtime | Primary path | Source enum | Status |
| --- | --- | --- | --- |
| LinuxSim | `LinuxSimRuntimeEntry -> LinuxSimRuntimeEntryAdoptionProbe -> AsciiRuntimeEntryAdoption` | `LinuxSimRuntimeSource::ScreenGraphAdoption` | primary screen graph adoption |
| GTK | `LinuxUConsoleGtkPageRegistryAdoption -> GtkRuntimeEntryAdoption` | `LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption` | primary page registry descriptors |
| LVGL | `LvglPrimaryScreenGraphRuntime -> LvglRuntimeEntryAdoption` | `LvglScreenGraphRuntimeSource::ScreenGraphAdoption` | primary descriptor runtime |

## Fallback Status

| Runtime | Fallback source | Status |
| --- | --- | --- |
| LinuxSim | `LinuxSimRuntimeSource::HardcodedFallback` | fallback-only |
| GTK | `LinuxUConsoleGtkPageRegistrySource::HardcodedFallback` | fallback-only |
| LVGL | `LvglScreenGraphRuntimeSource::HardcodedFallback` | fallback-only descriptor fallback |

Fallback remains testable through `fallbackUsed()` and source enum checks.

## Not Done

Phase 10 intentionally does not complete these migrations:

- real GTK widget rewrite
- real LVGL widget/menu rewrite
- fallback deletion
- full navigation stack replacement
- complete screen/page migration

## Guardrail

Runtime primary paths must not call `findUxPackById`, `UxPackRegistry`, or
`buildMenuForUxPack`. UX selection and `PresentationBundle` construction stay
in app-shell/probe/composition code. Runtime primary paths consume the
descriptor graph only.

LVGL primary descriptor runtime must not include `lvgl.h`, create `lv_obj_t`,
or branch on `BOARD_`.

## Phase 11 Recommendation

Phase 11 should be Real Renderer Descriptor Consumption. The first target
should stay on LinuxSim / ASCII because it proves renderer behavior with the
lowest device and widget-lifecycle risk. GTK and LVGL should follow only after
the ASCII renderer consumes descriptors as its concrete route source.
