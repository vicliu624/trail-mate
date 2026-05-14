# ui_lvgl_core

LVGL technical primitives.

May contain:

- LVGL themes
- primitive widgets
- technical navigation hosts
- reusable visual components
- input host primitives

Must not contain:

- target UX feature decisions
- app shell startup
- build entrypoints
- board facts
- Chat/Map/GPS runtime scheduling

Phase 8 structural migration creates this module as the LVGL core target module.
This batch only adds the transitional focus-group declaration needed by moved UX
renderers; the implementation remains in the ui_shared umbrella until the screen
host/runtime shell migration.
