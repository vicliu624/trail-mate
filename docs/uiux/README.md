# UI/UX Specification Index

`docs/uiux` is organized by object type rather than by implementation history.

Current directory rules:

- `foundation/`
  Cross-page design language, presentation system rules, public contracts, and authoring guidance.
- `pages/`
  Page-level specifications. A page document should explain one page object and should not redefine shared component contracts.
- `components/`
  Shared component specifications and implementation constraints. A component document should explain one reusable object and should not silently redefine page semantics.

Current foundation documents:

- `foundation/firmware_visual_style.md`
- `foundation/theme_system_spec.md`
- `foundation/theme_delivery_plan.md`
- `foundation/theme_authoring_guide.md`
- `foundation/theme_slot_inventory.md`
- `foundation/presentation_contract_inventory.md`

Current page documents:

- `pages/node_info_page.md`
- `pages/node_info_page_layer_popup_addendum.md`

Current component documents:

- `components/shared_map_viewport.md`
- `components/shared_map_viewport_impl.md`
- `components/shared_map_viewport_layer_popup_addendum.md`

When adding a new UI/UX document, decide the object boundary first:

1. Is it about global visual/presentation rules, one page, or one reusable component?
2. If it tries to define both page semantics and component contracts at once, the boundary is probably still mixed and should be split first.
3. Do not keep flattening page specs, component specs, and foundation contracts into one directory level.
