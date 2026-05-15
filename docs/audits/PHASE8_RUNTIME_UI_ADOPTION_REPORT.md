# Phase 8 Runtime UI Adoption Report

## Completed

Phase 8 now has descriptor-level runtime adoption bridges for the presentation
graph:

- GTK descriptor bridge:
  `modules/ui_gtk_runtime/*/gtk_uconsole_screen_graph_bridge.*`
- ASCII descriptor bridge:
  `modules/ui_ascii_runtime/*/ascii_screen_graph_bridge.*`
- LVGL descriptor bridge:
  `modules/ui_lvgl_ux_packs/runtime/lvgl_screen_graph_bridge.*`

Each bridge consumes:

- `product_composition::PresentationBundle`
- `hasUxMenu(...)`
- `hasScreenBindings(...)`
- the UI-specific `MenuRuntimeAdapter`
- the UI-specific `ScreenHostAdapter`

The bridge output is a descriptor graph only. It does not create GTK widgets,
LVGL objects, simulator windows, services, app shells, or UX packs.

## Still Fallback

The following runtime surfaces still use compatibility or hardcoded paths:

- real LVGL menu renderer
- real GTK page switch logic
- real simulator main UI selection
- hardcoded screen creation

These fallbacks remain compatibility fallbacks. Their existence means runtime
adapter smoke is proven, but real renderer adoption is not finished.

## Exit Conditions

Phase 8 runtime UI adoption is complete only when each real renderer consumes a
screen graph bridge:

- LVGL menu/page renderer consumes `LvglScreenGraphBridge`
- GTK uConsole page navigation consumes `GtkUConsoleScreenGraphBridge`
- simulator UI selection consumes `AsciiScreenGraphBridge`

Until then, the descriptor bridges are the adoption baseline and the hardcoded
renderers remain compatibility fallbacks.
