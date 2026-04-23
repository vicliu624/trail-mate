# Official Themes Bundle

This bundle is the reference external theme package for Trail Mate.

It exists for two reasons:

- make the current warm official visual language installable from outside the firmware image
- give third-party theme authors a concrete, complete example of the currently supported runtime theme surface

It contains:

- theme pack: `official-legacy-warm`

## What This Theme Covers Today

`official-legacy-warm` explicitly externalizes:

- all current public `color.*` slots
- the current live `component.*` runtime batch
- all currently wired semantic `asset.*` slots

That means it already covers the runtime surface used by:

- shared page/background/chrome colors
- shared top-bar and status-strip icons
- boot logo
- menu app icons
- screen sleep / screen saver chrome
- map self-position marker
- team location markers
- shared modal surface
- shared IME and toast surface
- tracker mode/list/action/modal surfaces
- team member semantic colors and member chips
- GPS route/track overlays, GPS popups, map-control surfaces, and the self-position marker
- node-info map markers, map readouts, info panel, and layer popup
- GNSS constellation colors, SNR colors, and status/sky/table surfaces
- SSTV image/progress/meter surfaces
- USB loading and service-panel status surfaces
- walkie-talkie meters and volume surface
- dashboard cards, dashboard widgets, and dashboard placeholder chrome
- map tile placeholder chrome

## Important Runtime Boundary

Current runtime support for external themes is real and now covers the shipped official UI surface.

This bundle is authoritative for:

- `tokens.ini`
- `components.ini` for the currently wired component batch
- `assets.ini`

Authoring boundary:

- `base_theme=builtin-ascii` remains the sanctioned minimal builtin baseline
- `builtin-legacy-warm` is kept only as a compatibility/fallback ID inside firmware, not as the public target for new third-party manifests

Runtime reduction update:

- the firmware has already dropped the shared built-in bitmap batch for boot, status-strip, menu app icons, and notification alert chrome
- those surfaces now rely on external theme assets first, with minimal text/symbol fallback when no external theme asset is installed
- the last page-private marker batch has now moved to external theme assets as well, including team-location markers and the GPS self-position marker
- firmware-side fallback for those surfaces is now text/symbol based rather than compiled bitmap based

Current runtime still does **not** yet consume every documented theme-layer surface category.
The remaining gap is mostly in broader authoring surface depth such as `font.*`, `metric.*`, and
`motion.*`, not in the historical warm UI implementation path itself.

What is live today is:

- `tokens.ini`
- `components.ini` for `top_bar`, menu chrome, `directory_browser`, `info_card`, `busy_overlay`,
  `system_notification`, `boot_splash`, `watch_face`, chat scaffold and bubble surfaces,
  `map_focus`, `instrument_panel`, `service_panel`, shared modal surface, shared `action_button.*`,
  IME, toast, USB, walkie-talkie, screen sleep / screen saver, dashboard chrome/widgets,
  map placeholder chrome, and the tracker / team / GNSS / SSTV / GPS / map page-private extraction batches
- `assets.ini`

Current live semantic color extraction also includes:

- `color.map.route`
- `color.map.track`
- `color.map.marker.border`
- `color.map.signal_label.bg`
- `color.map.node_marker`
- `color.map.self_marker`
- `color.map.link`
- `color.map.readout.*`
- `color.team.member.*`
- `color.gnss.system.*`
- `color.gnss.snr.*`
- `color.sstv.meter.mid`

So this bundle is intentionally honest: the current historical built-in warm UI path has now been
lifted onto real runtime theme surfaces, while the remaining incomplete work is about extending the
theme system beyond the already-shipped interface surface rather than chasing leftover warm
hardcodes in UI implementation files.

## Bundle Layout

- `package.ini`
- `DESCRIPTION.txt`
- `README.md`
- `themes/official-legacy-warm/manifest.ini`
- `themes/official-legacy-warm/tokens.ini`
- `themes/official-legacy-warm/components.ini`
- `themes/official-legacy-warm/assets.ini`
- `themes/official-legacy-warm/assets/*`

## Distribution Package

`python scripts/build_pack_repository.py --pack-root packs --site-root site` produces:

- `site/assets/packs/official-themes-1.0.0.zip`
- `site/data/packs.json`

The resulting zip unpacks its payload under `/trailmate/packs/themes/...`.
