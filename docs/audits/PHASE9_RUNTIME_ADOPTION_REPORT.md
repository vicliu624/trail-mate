# Phase 9 Runtime Adoption Report

## Current Scope

Phase 9 starts from the Phase 8 presentation graph and moves runtime consumers
onto stable module-owned presenters.

The Phase 9.1 baseline is now:

- Linux simulator descriptors live under `modules/ui_ascii_runtime`.
- uConsole GTK descriptors live under `modules/ui_gtk_runtime`.
- LVGL descriptors remain under `modules/ui_lvgl_ux_packs/runtime`.
- `legacy/app_implementations` no longer owns the ASCII/GTK runtime descriptor
  adapters introduced during Phase 8.

## Runtime Presenters

LinuxSim:

- Presenter: `AsciiRuntimeScreenGraphPresenter`
- Path: `modules/ui_ascii_runtime`
- Input: `product_composition::PresentationBundle`
- Output: `AsciiMenuLine` and `AsciiScreenDescriptor`
- Validation: `trailmate_ascii_runtime_screen_graph_presenter_smoke`

uConsole GTK:

- Presenter: `GtkUConsoleScreenGraphPresenter`
- Path: `modules/ui_gtk_runtime`
- Input: `product_composition::PresentationBundle`
- Output: `GtkMenuDescriptor` and `GtkScreenDescriptor`
- Validation: `trailmate_gtk_uconsole_screen_graph_presenter_smoke`

LVGL:

- Presenter: `LvglRuntimeScreenGraphPresenter`
- Path: `modules/ui_lvgl_ux_packs/runtime`
- Input: `product_composition::PresentationBundle`
- Output: `LvglMenuEntry` and `LvglScreenHostEntry`
- Validation: `trailmate_ui_lvgl_ux_packs_lvgl_runtime_screen_graph_presenter_smoke`

## Adoption Status

The presenters only consume `PresentationBundle`; they do not select UX packs,
create platform widgets, read app shells, or instantiate runtime services.

The legacy Linux simulator and uConsole roots now link the module-owned
descriptor runtimes instead of owning those adapters locally. This is a
burn-down move, not a legacy maintenance expansion.

## Remaining Fallback

The following paths still need Phase 9.2 real entry adoption:

- real Linux simulator UI selection
- real GTK uConsole page switching
- real LVGL menu/page renderer
- hardcoded screen creation fallback

Fallback remains contained until each real runtime entry consumes its presenter
and treats hardcoded routing as a compatibility fallback only.

## Phase 9.2 Entry Adoption

Phase 9.2 adds thin entry adoption helpers on top of the presenters:

- `AsciiRuntimeEntryAdoption`
- `GtkRuntimeEntryAdoption`
- `LvglRuntimeEntryAdoption`

The LinuxSim and GTK helpers live in stable runtime modules:

- `modules/ui_ascii_runtime`
- `modules/ui_gtk_runtime`

The final app-shell probes live under:

- `apps/linux_sim_shell`
- `apps/linux_uconsole_gtk`

They are probe glue for shell-selected UX packs. They must not become new
composition roots, and they must not be added to `legacy/`. The LVGL helper
remains under `modules/ui_lvgl_ux_packs/runtime`.

The detailed fallback map is tracked in
`docs/audits/PHASE9_RUNTIME_ENTRY_ADOPTION_REPORT.md`.

## Phase 9.3 Real Entry Adoption

Phase 9.3 moves beyond app-shell probe smoke coverage by adding real
entry-facing consumers that still avoid `legacy/app_implementations`:

- `LinuxSimRuntimeEntry` consumes `AsciiRuntimeEntryAdoption` through the
  final LinuxSim app-shell adoption probe.
- `LinuxUConsoleGtkPageRegistryAdoption` consumes `GtkRuntimeEntryAdoption`
  and exposes descriptor data that a GTK page registry can adopt.
- `LvglRuntimeAdoptionProbe` consumes `LvglRuntimeEntryAdoption` as the LVGL
  compatibility runtime landing point.

These paths do not create widgets, do not choose UX packs, and do not replace
the existing hardcoded UI behavior. They mark the hardcoded paths as contained
fallback until descriptors become the primary route/page/menu source.

The fallback inventory is tracked in
`docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md`.

## Phase 9 Rule

New runtime/presentation work must not be added to `legacy/`. If a legacy root
fails to compile because it owns runtime adoption logic, the fix is to move or
replace that logic under `modules/` and shrink the legacy root.
