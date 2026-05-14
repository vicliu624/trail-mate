# ui_gps_runtime

GPS page runtime scheduling helpers.

May contain:

- GPS runtime pump
- GPS UI refresh sink ports
- GPS page scheduling adapters

Must not contain:

- LVGL widget trees
- app shell startup
- build entrypoints
- board facts
- GPS parser implementation

This module was split from `modules/ui_shared` during the Phase 8 structural
migration batch. The old `ui/screens/gps/*runtime*` headers remain forwarding
headers.
