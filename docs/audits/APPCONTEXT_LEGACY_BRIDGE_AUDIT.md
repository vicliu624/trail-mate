# AppContext Legacy Bridge Audit

## Purpose

Phase 6 establishes product composition and app shell boundaries.

`AppContext` and the global app facade accessors remain historical composition
surfaces. This audit records them as bounded legacy bridges so new object graph
construction can move toward explicit target composition roots.

This audit does not remove `AppContext`, rewrite LVGL pages, or change ESP
startup order.

## Current Role

`AppContext` currently acts as a legacy service locator and compatibility
composition object for ESP-style targets.

It still owns or exposes:

- board/runtime initialization handles
- ChatService compatibility
- ContactService compatibility
- Team service compatibility
- mesh adapter compatibility
- BLE manager compatibility
- UI runtime facade compatibility
- event runtime hooks
- persisted app configuration compatibility

The shared `app::IAppFacade` and `app::appFacade()` accessors provide a similar
global compatibility surface for legacy UI code and target runtimes.

## Allowed Existing Use

Existing code may continue to use the bridge for bounded compatibility paths:

- ESP app startup and runtime lifecycle
- existing LVGL renderer/controller lifecycle paths
- ChatService and ContactService compatibility
- TeamUiStore and Team service compatibility
- settings pages that still use the legacy app facade
- tracker/team/settings legacy pages already documented as UI islands
- map viewport compatibility while tile/cache cleanup remains deferred

These uses are not Phase 6 blockers because they are historical, documented,
and guarded by the Phase 5 renderer and presentation checks.

## Forbidden New Use

New code must not add unrelated `AppContext` or `app::appFacade()` lookups for:

- new presentation models
- new renderer state
- new map tile/cache logic
- new protocol services
- new board/platform factories
- new target selection policy
- new source/sink construction from renderer/page code
- new Chat or Map UI paths that bypass Phase 5 presentation models

If a new feature needs one of these dependencies, it should be wired by a target
composition root or passed explicitly through a source/sink boundary.

## Boundary

The bridge may adapt old object ownership into Phase 6 bundles:

- `AppServicesBundle`
- `PresentationBundle`
- target composition roots
- explicit source/sink injection

The bridge must not become the default composition API for new code.

## Replacement Direction

The intended replacement path is:

```text
target composition root
  -> AppServicesBundle
  -> source/sink adapters
  -> PresentationBundle
  -> TargetAppShell
```

Legacy UI islands may continue to pass through `AppContext` until their own
cleanup phase, but each new composition path should prefer explicit ownership
from the target/app shell.

## Known Bridge Surfaces

| Surface | Current status | Phase 6 policy |
| --- | --- | --- |
| `apps/esp_pio/src/app_context.cpp` | legacy composition object | bounded bridge |
| `apps/esp_pio/src/app_runtime_access.cpp` | ESP startup bridge | bounded bridge |
| `apps/esp_idf/src/app_facade_runtime.cpp` | ESP-IDF app facade bridge | bounded bridge |
| `modules/core_sys/src/app/app_facade_access.cpp` | global facade pointer | compatibility only |
| `modules/ui_shared/src/ui/screens/settings` | legacy UI facade use | baseline |
| `modules/ui_shared/src/ui/screens/team` | legacy UI facade use | baseline |
| `modules/ui_shared/src/ui/screens/tracker` | legacy UI facade use | baseline |
| `modules/ui_shared/src/ui/widgets/map/map_viewport.cpp` | legacy map runtime use | baseline |

## Exit Interpretation

Phase 6 may close with this bridge still present if:

- new composition roots exist for Linux/uConsole pilots
- ESP composition ownership is documented or bridged
- new renderer/page code is prevented from expanding global service lookup
- future cleanup tracks know which legacy islands still depend on the bridge
