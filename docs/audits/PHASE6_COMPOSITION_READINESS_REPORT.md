# Phase 6 Composition Readiness Report

## Decision

Phase 6 is ready to close as a product composition and app shell boundary
baseline.

This decision does not mean every target startup path is fully clean. It means
object graph ownership is now explicit enough for future phases to build on
without expanding global composition drift.

## What Phase 6 Established

Phase 6 established the composition boundary above the Phase 5 presentation
layer:

```text
Target chooses.
Board describes.
Platform adapts.
Runtime schedules.
Capability provides.
Protocol interprets.
UseCase decides.
AppService coordinates.
PresentationModel projects.
Renderer draws.
CompositionRoot wires.
```

The new contracts are:

- `AppServicesBundle`
- `PresentationBundle`
- `ITargetAppShell`
- `CompositionStatus`

These contracts are intentionally small. They are explicit dependency bundles
and shell lifecycle contracts, not a replacement service locator.

## Composition Roots And Bridges

| Target or surface | Phase 6 result |
| --- | --- |
| Linux simulator | `LinuxSimCompositionRoot` owns fake source/sink/model graph and exports `PresentationBundle` |
| uConsole | `UConsoleCompositionRoot` owns Linux app services, uConsole app models, Chat presentation model, and Map presentation binding |
| ESP LVGL | documented bridge; startup order is not changed in Phase 6 |
| AppContext | documented as `LegacyAppContextBridge`; new expansion is forbidden |

## Boundary Rules Now Guarded

Phase 6 checker coverage confirms:

- `product_composition` contracts do not include platform/toolkit/AppContext
  runtime headers
- `ui_presentation` does not import product composition
- `AppServicesBundle` remains explicit and has no dynamic `get<T>()`
- `PresentationBundle` does not construct source/sink/model objects
- Linux simulator and uConsole composition roots exist
- ESP/AppContext bridge audits exist
- known legacy composition surfaces are baseline-recorded

## Remaining Legacy Composition Surfaces

The following remain intentionally bounded:

- ESP Arduino `AppContext`
- ESP-IDF app facade runtime
- global `app::appFacade()` compatibility pointer
- LVGL Chat page runtime construction of Chat presentation adapters
- LVGL GPS/map page runtime construction of Map presentation adapters
- dashboard GPS widget local presentation bridge
- GTK uConsole app state ownership of legacy app models
- uConsole legacy framebuffer shell service ownership

These are not Phase 6 blockers because they are documented in:

- `APPCONTEXT_LEGACY_BRIDGE_AUDIT.md`
- `ESP_LVGL_COMPOSITION_BRIDGE_AUDIT.md`
- `phase6_composition_legacy_baseline.json`
- `PHASE6_COMPOSITION_CONFORMANCE_MATRIX.md`

## Non-Goals Confirmed

Phase 6 did not:

- remove `AppContext`
- rewrite ESP startup
- rewrite LVGL pages
- rewrite ChatUiController
- rewrite GPSPageState
- rewrite map tile/cache/contour ownership
- convert target manifests into runtime configuration
- add a new service locator

## Phase 6 Exit Criteria

| Criterion | Status |
| --- | --- |
| Product composition architecture spec exists | pass |
| Product composition boundary audit exists | pass |
| `product_composition` contracts exist | pass |
| Linux simulator composition root smoke exists | pass |
| uConsole composition root smoke exists | pass |
| ESP LVGL composition bridge/audit exists | pass |
| AppContext bridge audit exists | pass |
| Phase 6 composition checker exists and passes | pass |
| PresentationWorkspace construction remains outside `ui_presentation` | pass |
| Legacy composition surfaces are baseline-recorded | pass |

## Phase 7 Input

Future phases may assume:

- Phase 5 presentation boundaries are stable
- target composition roots are the preferred place to create source/sink/model
  graphs
- renderer/page code should not create new app services
- `AppContext` is compatibility-only, not the default architecture

Future phases should reduce the baseline one surface at a time, starting with
the targets that have clear composition roots: Linux simulator and uConsole.
