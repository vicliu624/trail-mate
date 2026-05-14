# ui_map_runtime

Map runtime helpers.

May contain:

- map tile identity/source/cache ports
- tile resolver
- render queue
- map overlay projectors
- filesystem tile source adapters

Must not contain:

- LVGL widget tree ownership
- app shell startup
- build entrypoints
- board facts
- renderer layout policy

This module was split from `modules/ui_shared` during the Phase 8 structural
migration batch. The old `ui/map_tiles/*` and `ui/map_overlay/*` headers remain
forwarding headers.
