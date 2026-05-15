# Phase 10 Primary Path Migration Report

## Scope

Phase 10 changes the default path. It does not delete fallback, rewrite GTK
widgets, rewrite LVGL menu/page renderers, add UX packs, or let runtime entries
choose UX packs.

The migration rule is:

adoption descriptor path is primary; hardcoded path is fallback-only

## LinuxSim Primary Path

Previous status:

- LinuxSim hardcoded runtime routing was a contained fallback.
- `LinuxSimRuntimeEntry` consumed `LinuxSimRuntimeEntryAdoptionProbe`, but the
  primary/fallback source was not explicit.

Current primary path:

`LinuxSimRuntimeEntry -> LinuxSimRuntimeEntryAdoptionProbe -> AsciiRuntimeEntryAdoption -> AsciiRuntimeScreenGraphPresenter`

Current status:

- `LinuxSimRuntimeSource::ScreenGraphAdoption` is the default source when
  adoption loads.
- `LinuxSimRuntimeSource::HardcodedFallback` is used only when adoption fails.
- `usingPrimaryScreenGraph()` and `fallbackUsed()` expose the active path.

Fallback status:

fallback-only after Phase 10.1

Deletion condition:

simulator renderer no longer needs hardcoded routing.

## GTK Primary Path

Previous status:

- GTK hardcoded page registry was a contained fallback.
- `LinuxUConsoleGtkPageRegistryAdoption` exposed descriptors, but the
  primary/fallback source was not explicit.

Current primary path:

`LinuxUConsoleGtkPageRegistryAdoption -> GtkRuntimeEntryAdoption -> GtkUConsoleScreenGraphPresenter`

Current status:

- `LinuxUConsoleGtkPageRegistrySource::ScreenGraphAdoption` is the default
  source when adoption loads.
- `LinuxUConsoleGtkPageRegistrySource::HardcodedFallback` is used only when
  adoption fails.
- `usingPrimaryScreenGraph()` and `fallbackUsed()` expose the active path.

Fallback status:

fallback-only after Phase 10.2

Deletion condition:

GTK page registry consumes descriptors as its only page source.

## LVGL Primary Descriptor Path

Previous status:

- LVGL hardcoded menu/page creation was a contained fallback.
- `LvglRuntimeAdoptionProbe` proved compatibility runtime descriptor
  consumption, but the descriptor runtime was not the explicit primary source.

Current primary descriptor path:

`LvglPrimaryScreenGraphRuntime -> LvglRuntimeEntryAdoption -> LvglRuntimeScreenGraphPresenter`

Current status:

- `LvglScreenGraphRuntimeSource::ScreenGraphAdoption` is the default source
  when adoption loads.
- `LvglScreenGraphRuntimeSource::HardcodedFallback` is used only when adoption
  fails.
- `usingPrimaryScreenGraph()` and `fallbackUsed()` expose the active path.

Fallback status:

fallback-only after Phase 10.3

Real widget migration:

deferred

Deletion condition:

LVGL renderers consume descriptor runtime before creating menu/page objects.

## Not Done

- real GTK widget hierarchy rewrite
- real LVGL widget/menu rewrite
- fallback deletion
- full navigation stack replacement
- all screen/page migration

## Checker Status

`tools/architecture/check_phase10_primary_path_ready.py` verifies that the
three primary paths expose source enums, primary/fallback probes, reports, and
forbidden-token guardrails.

## Phase 11 Recommendation

Phase 11 should target real renderer descriptor consumption:

- first consume ASCII descriptors as renderer input without hardcoded route
  lookup
- then let GTK page registry consume descriptors as the concrete page list
- then migrate LVGL device renderers from descriptor-primary runtime into real
  widget/menu construction
