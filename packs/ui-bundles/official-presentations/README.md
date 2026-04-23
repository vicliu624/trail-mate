# Official Presentations Bundle

This bundle is the reference external presentation package for Trail Mate.

It has two jobs:

- provide an installable official presentation pack that mirrors the current built-in UI
- give third-party presentation authors a full-coverage starting point across every public archetype

It contains:

- presentation pack: `official-default`
- presentation pack: `official-directory-stacked`

`official-default` explicitly declares layout files for all current public archetypes:

- `menu_dashboard`
- `boot_splash`
- `watch_face`
- `chat_list`
- `chat_conversation`
- `chat_compose`
- `directory_browser`
- `map_focus`
- `instrument_panel`
- `service_panel`

`official-default` intentionally does not declare `base_presentation=builtin-default`.
That builtin ID is a firmware fallback detail, not the public authoring target for new packs.
Derived packs should inherit from `official-default`, just as `official-directory-stacked` does.

`official-directory-stacked` inherits from `official-default` and only overrides
`directory_browser`, so it remains full-coverage through the base-presentation chain.

The firmware builtin fallback for `menu_dashboard` is now a plain `simple-list`.
The current launcher/grid menu is intentionally owned by `official-default`, not by
the builtin fallback path.

The `menu_dashboard`, `boot_splash`, `watch_face`, `directory_browser`,
`chat_conversation`, `chat_compose`, `map_focus`, `instrument_panel`, and `service_panel` layout
files now do more than pick a legacy renderer variant. They are the first public examples of the
v1 declarative schema batch, using keys such as:

- `archetype=page_archetype.menu_dashboard`
- `layout=launcher-grid`
- `archetype=page_archetype.boot_splash`
- `archetype=page_archetype.watch_face`
- `archetype=page_archetype.directory_browser`
- `archetype=page_archetype.chat_conversation`
- `archetype=page_archetype.chat_compose`
- `archetype=page_archetype.map_focus`
- `archetype=page_archetype.instrument_panel`
- `archetype=page_archetype.service_panel`
- `region.menu.app_grid.height_pct=...`
- `region.boot.log.align=start|center|end`
- `region.watchface.primary.width_pct=...`
- `layout.axis=row|column`
- `region.directory.selector.position=first|last`
- `component.directory.selector_button.height=...`
- `region.chat.action_bar.position=first|last`
- `region.map.overlay.primary.position=top_left|top_right|bottom_left|bottom_right`
- `layout.body.flow=row|column`
- `region.service.primary_panel.pad_all=...`

For `menu_dashboard`, `boot_splash`, and `watch_face`, the current batch controls display-surface
geometry only; page-private internal widgets remain firmware-owned.

For `instrument_panel` and `service_panel`, the current batch controls archetype scaffold
geometry only; page-private internal widgets are still firmware-owned.

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `presentations/official-default/manifest.ini`
- `presentations/official-default/layouts/*.ini`
- `presentations/official-directory-stacked/manifest.ini`
- `presentations/official-directory-stacked/layouts/directory_browser.ini`

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/official-presentations-1.0.0.zip`
- `site/data/packs.json`

The resulting zip unpacks its payload under `/trailmate/packs/presentations/...`.

## Why This Bundle Exists

Without this bundle, third-party presentation authors would still need to infer too much from
firmware source code. This package makes the intended public surface concrete:

- every public archetype has a declared layout file
- the runtime can enumerate and activate the presentations
- builtin fallback still protects the device if an external presentation is incomplete
- third-party authors get a public sample that does not leak builtin official fallback IDs into new manifests
