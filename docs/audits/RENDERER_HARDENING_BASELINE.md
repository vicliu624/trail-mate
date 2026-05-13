# Renderer Hardening Baseline

## Purpose

Phase 5.9 introduces renderer hardening without pretending the legacy renderer
is already clean.

This baseline records known renderer legacy islands that are allowed to remain
while strict surfaces are locked.

## Policy

Default renderer hardening checks must:

- fail on strict surface violations
- fail when migrated paths lose their presentation model locks
- report baseline legacy islands without failing

Strict surfaces include:

- `ui_presentation/*`
- ASCII/headless probes
- Chat/Team/Map presentation sources and sinks
- migrated Chat/Team/Map paths
- uConsole `presentation_workspace` bridge

Baseline islands are not a free pass for new work. New renderer code must prefer
presentation models. New direct runtime reads must either move behind a
source/sink boundary or be added to the baseline with an explicit reason.

## Baseline File

The machine-readable baseline lives at:

```text
tools/architecture/phase5_renderer_legacy_baseline.json
```

It records:

- legacy island id
- path
- allowed tokens
- reason
- expected future cleanup phase

## Known Legacy Island Summary

| Island | Status |
| --- | --- |
| `ChatUiController` | legacy application controller |
| `GPSPageState` | legacy LVGL map/GPS page state |
| `ui/widgets/map/map_viewport` | legacy tile renderer/runtime |
| LVGL dashboard widgets | incremental snapshot migration target |
| uConsole map model | bounded platform application model |
| uConsole chat/dashboard models | bounded platform application models |
| uConsole GTK overview logic | legacy GPS compatibility renderer |
| tracker route overlays | deferred renderer cleanup |

## Interpretation

The baseline should shrink over time.

Adding a new baseline entry is an architecture decision, not a workaround. It
must explain why the coupling remains outside the current phase and which future
cleanup should own it.
