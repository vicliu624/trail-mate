# Phase 9 Legacy Burn-down Report

## Burned Down In Phase 9.1

The Phase 8 ASCII and GTK descriptor adapters were initially located under
`legacy/app_implementations`. Phase 9 moves them to module-owned runtime
surfaces:

| Surface | Previous owner | New owner | Status |
| --- | --- | --- | --- |
| ASCII menu runtime adapter | `legacy/app_implementations/linux_sim` | `modules/ui_ascii_runtime` | moved out of legacy |
| ASCII screen host adapter | `legacy/app_implementations/linux_sim` | `modules/ui_ascii_runtime` | moved out of legacy |
| ASCII screen graph bridge | `legacy/app_implementations/linux_sim` | `modules/ui_ascii_runtime` | moved out of legacy |
| GTK menu runtime adapter | `legacy/app_implementations/linux_uconsole` | `modules/ui_gtk_runtime` | moved out of legacy |
| GTK screen host adapter | `legacy/app_implementations/linux_uconsole` | `modules/ui_gtk_runtime` | moved out of legacy |
| GTK screen graph bridge | `legacy/app_implementations/linux_uconsole` | `modules/ui_gtk_runtime` | moved out of legacy |

## Still Contained

These code-level legacy adapters remain governed by
`docs/audits/LEGACY_BURNDOWN_REGISTER.md`:

- `LegacyChatDeliveryActionBridge`
- `LegacyChatDeliveryEventBridge`
- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`
- `LegacyMapOverlaySource`

## Next Burn-down Target

The next Phase 9 legacy burn-down target is the chat delivery bridge pair. The
exit direction is to replace the `LegacyChatDelivery*Bridge` names with stable
chat runtime port adapters, or to make the old headers deprecated forwarding
aliases once no main runtime code includes them directly.

## Rule

Phase 9 does not add new runtime ownership under `legacy/`. Compilation
failures in a legacy root should be treated as a signal to move the runtime
surface into a stable module or to replace the legacy adapter.

## Phase 9.2 Correction

Runtime entry adoption helpers are not maintained under
`legacy/app_implementations`. The Phase 9.2 entry adoption surfaces live in
`modules/ui_ascii_runtime`, `modules/ui_gtk_runtime`, and
`modules/ui_lvgl_ux_packs`; final app-shell probes live under
`apps/linux_sim_shell` and `apps/linux_uconsole_gtk`.

If a future Phase 9 task needs behavior that currently sits in
`legacy/app_implementations`, the task should burn down that behavior into a
stable owner instead of adding another compatibility helper inside the legacy
tree.

## Phase 9.3 Correction

Real runtime entry adoption also stays out of `legacy/app_implementations`.
The LinuxSim runtime entry and GTK page-registry adoption live under final
app-shell directories, while the LVGL compatibility runtime adoption probe
lives under `modules/ui_lvgl_ux_packs`.

Legacy implementation roots may continue to exist as historical fallback, but
new Phase 9 adoption code must either live in a stable module or in the final
app-shell surface that owns product startup semantics.
