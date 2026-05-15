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
