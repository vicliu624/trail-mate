# Product Composition Architecture Specification

## Purpose

This specification defines how Trail Mate product targets assemble runtime,
app services, presentation models, and renderers.

Phase 6 uses this specification to move object graph construction toward
explicit target/app shell composition roots.

## Core Rule

CompositionRoot wires.

It does not decide business behavior.

The product architecture responsibility chain is:

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

## Composition Root Responsibilities

A composition root may:

- read target manifest or compile target identity
- select board package
- create platform adapter factories
- create runtime context
- create app services
- create presentation sources/sinks
- create presentation models
- create `PresentationWorkspace`
- create renderer shell
- connect lifecycle start/stop hooks

A composition root must not:

- interpret protocol payloads
- implement business decisions
- directly render UI widgets
- own packet parsing
- own GPS parsing
- own tile cache behavior
- own Chat message policy
- infer pending/failure state

## Target App Shell

`TargetAppShell` owns:

- renderer lifecycle
- event loop integration
- presentation workspace binding
- input dispatch into presentation actions
- shell-specific timers
- shell startup/shutdown

`TargetAppShell` must not own:

- protocol interpretation
- board pin facts
- storage schema
- business policy
- platform adapter semantics

## Platform Factory

Platform factories may create:

- radio adapter
- GPS byte stream
- BLE host adapter
- storage backend
- display driver host
- input driver host
- timer/clock/queue primitives

Platform factories must not:

- select product feature set
- interpret Meshtastic/MeshCore semantics
- create UI page business logic
- own AppService coordination

## Board Package

Board packages describe hardware facts:

- pins
- buses
- power rails
- display dimensions
- available peripherals
- electrical constraints

Board packages must not:

- decide product features
- create app services
- interpret radio protocols
- render UI
- own runtime scheduling policy

## Target Manifest

Target manifests are product configuration contracts.

They may select:

- board package
- platform profile
- shell profile
- capabilities
- authority sources

They must not become arbitrary runtime configuration systems in Phase 6.

## AppContext Bridge

Existing `AppContext` is a legacy service locator.

Phase 6 may introduce:

- `LegacyAppContextBridge`
- `AppServicesBundle`
- `PresentationBundle`

The bridge must shrink over time. New code must not add unrelated service
lookups to `AppContext`.

## Object Lifetime

Target composition roots own static or long-lived objects.

No renderer may create app services.

No presentation model may create source/sink objects.

No source/sink may create renderer objects.

`PresentationWorkspace` may expose model pointers that are owned by the target
composition root or app shell. It must not create those models.

## Allowed Dependency Direction

```text
Target/App Composition Root
  -> platform factories
  -> app services
  -> presentation sources/sinks
  -> presentation models
  -> presentation workspace
  -> renderer shell
```

Renderers may consume presentation snapshots and send presentation actions.

Renderers must not reach backward into platform factories, app-service
construction, or board facts.

## Exit Criteria

Phase 6 is successful when:

- each major target has an explicit composition root or documented legacy bridge
- `PresentationWorkspace` is assembled by target/app shell code
- `AppContext` usage is bounded
- product composition contracts exist
- checker prevents new object graph construction in renderer/page code
- target/board/platform wiring rules are documented and checked
