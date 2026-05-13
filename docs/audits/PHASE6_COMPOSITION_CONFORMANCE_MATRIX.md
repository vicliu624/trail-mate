# Phase 6 Composition Conformance Matrix

## Purpose

This matrix records the Phase 6 product composition and app shell boundary
status.

A target or boundary is Phase 6-ready when:

- object graph ownership is explicit
- presentation models are assembled outside `ui_presentation`
- app services are not created by renderers
- legacy composition surfaces are documented
- checker coverage prevents new drift

Phase 6-ready does not mean all global state or legacy startup code has been
removed.

## Matrix

| Area | Composition contract | Root or bridge | Presentation binding | Legacy registered | Checker coverage | Status |
| --- | --- | --- | --- | --- | --- | --- |
| Product composition spec | yes | n/a | n/a | n/a | yes | ready |
| `product_composition` contracts | yes | n/a | `PresentationBundle` | n/a | yes | ready |
| Linux simulator | yes | `LinuxSimCompositionRoot` | full `PresentationWorkspace` with fakes | no | yes | ready |
| uConsole GTK/Linux | yes | `UConsoleCompositionRoot` pilot | Chat/Map presentation binding | GTK app state still legacy | yes | ready-with-legacy |
| ESP LVGL | bridge documented | ESP LVGL composition bridge audit | future bridge/root | `AppContext` and app facade runtime | yes | ready-with-legacy |
| AppContext / app facade | bridge documented | `LegacyAppContextBridge` audit | future explicit bundle binding | yes | yes | ready-with-legacy |
| LVGL Chat page runtime | partial | legacy page bridge | Chat/Team presentation models | yes | baseline | ready-with-legacy |
| LVGL GPS/map page runtime | partial | legacy page bridge | Map presentation model | yes | baseline | ready-with-legacy |
| uConsole tile/cache/contour | no pure root yet | legacy app model | Map workspace status only | yes | baseline | ready-with-legacy |

## Status Definitions

### ready

The composition boundary is explicit and has no major unresolved legacy
ownership in the Phase 6 scope.

### ready-with-legacy

The boundary is explicit, but older construction or global lookup remains
documented and guarded by baseline/checker coverage.

### blocked

The boundary is missing or unsafe for Phase 6 exit.

## Phase 6 Exit Interpretation

Phase 6 exits successfully if all areas are either:

- ready
- ready-with-legacy

No area may remain blocked.

The key Phase 6 result is not the removal of all legacy object construction.
The result is that target/app shell composition ownership is now named,
bounded, and automatically checked.
