# Phase 9 Final Readiness Report

## Scope

Phase 9.6 closes the Phase 9 architecture work. It does not add new presenter,
entry adoption, UX pack, renderer, or legacy compatibility layers.

The Phase 9 closeout distinction is:

- Phase 9 proves runtime adoption paths and burns down selected legacy
  adapters from main runtime ownership.
- Phase 10 makes one adopted path the primary runtime path.

## Runtime Adoption

| Runtime | Presenter status | Entry adoption status | Real entry status | Remaining fallback |
| --- | --- | --- | --- | --- |
| ASCII / LinuxSim | `AsciiRuntimeScreenGraphPresenter` done in `modules/ui_ascii_runtime` | `AsciiRuntimeEntryAdoption` done | `LinuxSimRuntimeEntry` consumes the adoption probe, but descriptors are not yet the primary runtime route source | LinuxSim hardcoded runtime routing |
| GTK / uConsole | `GtkUConsoleScreenGraphPresenter` done in `modules/ui_gtk_runtime` | `GtkRuntimeEntryAdoption` done | `LinuxUConsoleGtkPageRegistryAdoption` exposes descriptor data, but the page registry is not yet descriptor-primary | GTK hardcoded page registry |
| LVGL | `LvglRuntimeScreenGraphPresenter` done in `modules/ui_lvgl_ux_packs` | `LvglRuntimeEntryAdoption` done | `LvglRuntimeAdoptionProbe` proves compatibility runtime consumption, but real renderers are not yet descriptor-primary | LVGL hardcoded menu/page creation |

The runtime adoption path is real enough to test:

`PresentationBundle -> RuntimeEntryAdoption -> RuntimeScreenGraphPresenter`

It is not yet the primary renderer path. That is the Phase 10 boundary.

Readiness summary:

- ASCII presenter: done; entry adoption done; primary route migration remains.
- GTK presenter: done; entry adoption done; primary page registry migration remains.
- LVGL presenter: done; probe adoption done; primary menu/page renderer migration remains.

## Legacy Burn-down

| Surface | Phase 9 status | Stable owner | Remaining compatibility |
| --- | --- | --- | --- |
| ChatDelivery | main runtime callers removed; burned down to formal adapters | `ChatDeliveryActionPortAdapter`, `ChatDeliveryEventProjectionAdapter`, `IChatDeliveryActionPort`, `IChatDeliveryEventPort` in `modules/ui_chat_runtime` | deprecated alias headers and legacy alias tests only |
| KeyVerification | main runtime callers removed; burned down to formal source/sink/session adapter | `KeyVerificationPresentationSource`, `KeyVerificationActionSink`, `KeyVerificationSessionAdapter` in `modules/ui_key_verification_runtime` | deprecated alias headers and legacy alias tests only |
| MapOverlay | main runtime callers removed; burned down to stable snapshot/projection adapters | `MapOverlaySnapshotSource`, `MapOverlayProjectionAdapter` in `modules/ui_map_runtime` | deprecated alias headers and legacy alias tests only |

Legacy headers are not primary runtime APIs. Their deletion window opens after
downstream compatibility includes are gone.

## Remaining Fallback

These fallbacks remain intentionally contained:

| Fallback | Owner | New path | Exit condition |
| --- | --- | --- | --- |
| LinuxSim hardcoded runtime routing | final LinuxSim app shell and historical simulator runtime | `LinuxSimRuntimeEntry -> LinuxSimRuntimeEntryAdoptionProbe -> AsciiRuntimeEntryAdoption` | `AsciiRuntimeEntryAdoption as primary source` for simulator routing |
| GTK hardcoded page registry | final GTK app shell page-registry adoption and historical GTK page registry | `LinuxUConsoleGtkPageRegistryAdoption -> GtkRuntimeEntryAdoption -> GtkUConsoleScreenGraphPresenter` | GTK page registry consumes descriptors as primary page source |
| LVGL hardcoded menu/page creation | `modules/ui_lvgl_ux_packs` compatibility runtime plus device renderers | `LvglRuntimeAdoptionProbe -> LvglRuntimeEntryAdoption -> LvglRuntimeScreenGraphPresenter` | LVGL compatibility runtime consumes descriptors before creating menu/page objects |

The fallback ledger remains the authoritative itemized tracker for fallback
owner, reason, new path, exit condition, and checker status.

## Consistency Baseline

Phase 9 final readiness requires the following documents to agree:

- `docs/audits/LEGACY_BURNDOWN_REGISTER.md`
- `docs/audits/PHASE9_LEGACY_BURNDOWN_REPORT.md`
- `docs/audits/PHASE9_FALLBACK_CONTAINMENT_LEDGER.md`

The shared status for ChatDelivery, KeyVerification, and MapOverlay is:

main runtime callers removed; deprecated aliases only

The shared status for LinuxSim, GTK, and LVGL hardcoded UI paths is:

contained fallback with owner and exit condition

## Phase 10 Entry Recommendation

First target:

LinuxSim / ASCII primary path

Proposed Phase 10 first cut:

LinuxSim hardcoded runtime routing -> `AsciiRuntimeEntryAdoption as primary source`

Why this is the right first cut:

- It avoids real device memory, input, and LVGL layout risk.
- It does not require GTK widget or page-registry replacement.
- It proves the ScreenGraphPresenter path can become primary before touching
  device renderers.
- Failure cost is low because the existing simulator fallback can remain
  visible and testable.

Expected risk:

Low to medium. The risk is output parity and fallback detection, not device
memory pressure, platform widget lifecycle, or board-specific layout behavior.

## Phase 9.6 Guardrail

No new Phase 9 work should add runtime ownership under `legacy/`, introduce a
new presenter/adoption layer, or rename remaining fallback as completed primary
path migration. Phase 10 starts only when one real runtime path makes the
adoption path primary.
