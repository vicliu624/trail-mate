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

## Burned Down In Phase 9.4

Phase 9.4 burns down the Chat delivery bridge pair from real implementation
ownership into stable `ui_chat_runtime` ports and adapters.

| Surface | Previous owner | New owner | Status |
| --- | --- | --- | --- |
| `LegacyChatDeliveryActionBridge` | `modules/ui_legacy_adapters` real bridge implementation | `ChatDeliveryActionPortAdapter` and `IChatDeliveryActionPort` in `modules/ui_chat_runtime` | main runtime callers removed; old headers are deprecated aliases |
| `LegacyChatDeliveryEventBridge` | `modules/ui_legacy_adapters` real bridge implementation | `ChatDeliveryEventProjectionAdapter` and `IChatDeliveryEventPort` in `modules/ui_chat_runtime` | main runtime callers removed; old headers are deprecated aliases |

The former `modules/ui_legacy_adapters/src/legacy_chat_delivery_*_bridge.cpp`
implementation files are removed from the build. Compatibility headers remain
only as forwarding aliases for downstream includes.

## Still Contained

These code-level legacy adapters remain governed by
`docs/audits/LEGACY_BURNDOWN_REGISTER.md`:

- `LegacyKeyVerificationSource`
- `LegacyKeyVerificationActionSink`
- `LegacyMapOverlaySource`

## Next Burn-down Target

The next Phase 9 legacy burn-down target should be selected from
`LegacyKeyVerificationSource`, `LegacyKeyVerificationActionSink`, and
`LegacyMapOverlaySource`. The Chat delivery bridge pair is no longer a main
runtime fallback; its remaining work is compatibility alias deletion.

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
