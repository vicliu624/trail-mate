# ui_chat_runtime

Chat runtime scheduling helpers.

May contain:

- chat event pump
- UI refresh sink ports
- chat page runtime scheduling helpers

Must not contain:

- LVGL widget trees
- app shell startup
- build entrypoints
- board facts
- renderer-specific layout

This module was split from `modules/ui_shared` during the Phase 8 structural
migration batch. The old `ui/screens/chat/*` headers remain forwarding headers.
