# ui_lvgl_ux_packs

Device-specific LVGL UX pack renderers and common UX-pack widgets.

May contain:

- page sets
- renderer variants
- modal variants
- picker variants
- input binding sets
- layout profiles
- feature-depth decisions

Must not contain:

- board pin facts
- SDK/HAL calls
- build entrypoints
- protocol interpretation
- direct filesystem tile paths

The Phase 8 structural migration starts this module with the already-extracted
Team position picker and key verification modal renderer.
