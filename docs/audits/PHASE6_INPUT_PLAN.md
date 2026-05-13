# Phase 6 Input Plan

## Purpose

This document records what Phase 6 receives from Phase 5.

Phase 5 established UI presentation boundaries. Phase 6 may build on those
boundaries, but must not bypass them.

## Phase 6 May Assume

Phase 6 may assume:

- presentation models exist for Device, GPS, Mesh, Settings, Chat, Team Chat,
  and Map
- renderers should consume snapshot/action boundaries
- `ui_presentation` is platform/toolkit independent
- legacy adapters isolate compatibility surfaces
- `PresentationWorkspace` can compose target presentation capabilities
- ASCII/headless probes can validate the presentation graph
- renderer hardening checkers prevent major regressions
- legacy islands are registered and bounded

## Phase 6 Must Not

Phase 6 must not:

- call GPS/runtime/store/radio directly from new renderer paths
- bypass `ChatWorkspaceModel` for new chat UI paths
- bypass Team `ChatWorkspaceModel` / Team source/sink for new Team text UI paths
- bypass `MapWorkspaceModel` for new map workspace state
- map Team to DirectPeer or Channel
- put tile/cache into `MapWorkspaceSnapshot`
- put pending/failure queues into `ChatWorkspaceModel`
- make `PresentationWorkspace` construct source/sink objects
- treat legacy baseline entries as permission for unrelated new coupling

## Recommended Phase 6 Tracks

### Track A: Product Composition / App Shell

- move object graph construction into composition roots
- reduce `AppContext` leakage
- centralize board/target wiring
- compose `PresentationWorkspace` per target
- keep source/sink construction outside `ui_presentation`

### Track B: Legacy UI Island Cleanup

- split `ChatUiController`
- split `GPSPageState`
- split map viewport/tile renderer
- introduce `KeyVerificationPresentation`
- introduce Team rich payload UI
- reduce route/tracker overlay coupling

### Track C: Renderer Strictness

- gradually move baseline violations into strict rules
- harden LVGL renderer paths
- harden GTK/uConsole renderer paths
- keep ASCII/headless probes as strict examples
- avoid adding direct runtime reads to renderer code

### Track D: Runtime Ownership Cleanup

- structured pending/failure projection
- tile source/cache abstraction
- Team location action sink
- Team command action sink
- map overlay presentation sources

## Phase 6 Entry Checks

Before Phase 6 changes land, run:

```text
python tools/architecture/check_phase5_final_ready.py
```

When changing a specific Phase 5 boundary, also run the focused checker:

```text
python tools/architecture/check_phase5_ready.py
python tools/architecture/check_phase5_workspace_ready.py
python tools/architecture/check_phase5_renderer_ready.py
```

## Migration Rule

If Phase 6 touches a legacy island, it should choose one of two paths:

1. preserve the existing bounded legacy contract and avoid expanding it
2. migrate the island behind a stricter presentation model/source/sink boundary

It should not create a third path where renderer, platform runtime, store, and
business service state are mixed again.
