# Removed Legacy Roots

This record replaces the removed root source tree with a textual history only.
No legacy source archive is retained under `docs/archive`.

## legacy/

Historical role:
- Repository-level container for pre-final implementation roots and governance
  notes.

Final replacement owner:
- `apps/*`
- `builds/*`
- `boards/*`
- `modules/*`
- `platform/*`
- `docs/archive/REMOVED_LEGACY_ROOTS.md`

Removal reason:
- Root implementation ownership has moved to final app, build, board, module,
  and platform owners.
- Final guardrails now forbid reintroducing the root source tree.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Continue deleting remaining compatibility aliases and fallback-only paths by
  their specific ledgers.

## legacy/app_implementations/esp_idf

Historical role:
- Pre-refactor ESP-IDF app implementation root.
- Owned component registration, sdkconfig defaults, startup/loop/runtime
  config, and ESP-specific adapters.

Final replacement owner:
- `builds/esp_idf`
- `apps/esp32_lvgl`
- `platform/esp/radio`
- `modules/product_composition`
- `boards/tab5`
- `boards/tdisplayp4`

Removal reason:
- ESP-IDF final owner extraction completed for the migrated Batch 2 surfaces.
- `apps/esp32_lvgl` no longer links the ESP-IDF implementation adapter.
- Target defaults and source ownership are tracked by final owners.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Continue migrating deferred ESP GPS/team/map ownership from the existing audit
  into final modules when those areas are touched.

## legacy/app_implementations/esp_pio

Historical role:
- Pre-refactor PlatformIO/Arduino implementation root.
- Held historical runtime access and wrapper assumptions for embedded PIO
  builds.

Final replacement owner:
- `builds/pio_nrf52`
- `apps/nrf52_node`
- `modules/product_composition`
- `modules/ui_presentation`

Removal reason:
- The nRF52 wrapper no longer depends on the historical PIO include/source
  root.
- Product target and UX binding are now owned by final product composition and
  app shell metadata.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Keep root PlatformIO cleanup separate from this removal if future ESP Arduino
  routes are retired or migrated.

## legacy/app_implementations/gat562_mesh_evb_pro

Historical role:
- Pre-refactor GAT562 Mesh EVB Pro app/runtime root.
- Mixed device startup, protocol/debug behavior, and board-adjacent runtime
  glue.

Final replacement owner:
- `apps/nrf52_node`
- `builds/pio_nrf52`
- `boards/gat562_mesh_evb_pro`
- `modules/ui_headless_runtime`
- `modules/product_composition`

Removal reason:
- The active nRF52 build wrapper no longer references the historical root.
- GAT562 hardware facts and headless product routing now have final owners.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Continue moving any future concrete GAT562 runtime behavior into
  `apps/nrf52_node`, platform modules, or stable runtime modules as needed.

## legacy/app_implementations/linux_rpi

Historical role:
- Pre-refactor Raspberry Pi / Cardputer Zero Linux bring-up root.
- Carried local CMake/SCons/script workflow history for Linux device work.

Final replacement owner:
- `apps/linux_sim_shell` for the current Cardputer Zero fallback route.
- `builds/linux_cmake`
- `platform/linux`
- `boards/cardputerzero`

Removal reason:
- Batch 3 recorded Cardputer Zero as an active-with-fallback target through
  final target metadata instead of a legacy Linux root.
- Root legacy source is no longer an acceptable final architecture state.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Decide whether Cardputer Zero receives a dedicated final app shell or remains
  on the documented Linux simulator-shell fallback route.

## legacy/app_implementations/linux_sim

Historical role:
- Pre-refactor Linux simulator implementation root.
- Previously owned simulator composition root, app-local CMake, scripts, and
  smoke/probe code.

Final replacement owner:
- `apps/linux_sim_shell`
- `builds/linux_cmake`
- `modules/ui_ascii_runtime`
- `platform/linux`
- `modules/product_composition`

Removal reason:
- LinuxSim local root collapse moved validation to the final app shell.
- The legacy-local root had been reduced to archive-only and is now removed.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Delete fallback-only simulator runtime paths only when the dedicated fallback
  deletion ledger says the primary descriptor route no longer needs them.

## legacy/app_implementations/linux_uconsole

Historical role:
- Pre-refactor Linux uConsole GTK implementation root.
- Previously owned GTK composition root, old widget/page sources, scripts,
  packaging history, and local CMake smoke targets.

Final replacement owner:
- `apps/linux_uconsole_gtk`
- `builds/linux_cmake`
- `platform/linux/uconsole`
- `modules/ui_gtk_runtime`
- `modules/product_composition`

Removal reason:
- uConsole final app shell and GTK descriptor page registry now own the active
  route.
- The legacy-local root had been reduced to archive-only and is now removed
  rather than retained as a source archive.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Continue GTK descriptor-bound page/controller migration without copying old
  root source wholesale into a new owner.
- Re-establish final packaging ownership for Debian packaging under final build
  or app shell ownership when packaging work resumes.

## legacy/app_implementations/linux_unoq

Historical role:
- Placeholder pre-final Linux UNO Q implementation root.

Final replacement owner:
- No active app shell owner until a product decision makes UNO Q real.
- Future ownership must be under `apps/*`, `builds/linux_cmake`, and
  `platform/linux`, not under a restored source root.

Removal reason:
- Placeholder legacy roots are not final architecture.
- No active final target currently depends on this source root.

Source disposition:
- Historical source root removed.
- No source archive retained under `docs/archive`.

Known follow-up:
- Record a final target/app/build owner before adding any future UNO Q source.
