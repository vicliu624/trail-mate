# Phase 9 Fallback Containment Ledger

Phase 9.3 keeps hardcoded routing alive as contained fallback while real
runtime entries begin consuming the presentation graph. Fallback presence is
acceptable only when the current owner, new path, exit condition, and checker
status are explicit.

| Fallback | Current owner | Why fallback remains | New path | Exit condition | Checker status |
| --- | --- | --- | --- | --- | --- |
| LinuxSim hardcoded runtime routing | final `apps/linux_sim_shell` runtime entry plus historical simulator runtime | the simulator UI has not been rewritten around screen graph descriptors | `LinuxSimRuntimeEntry -> LinuxSimRuntimeEntryAdoptionProbe -> AsciiRuntimeEntryAdoption` | simulator runtime routing consumes `AsciiRuntimeEntryAdoption` descriptors as its primary route source | contained fallback |
| GTK hardcoded page registry | final `apps/linux_uconsole_gtk` page registry adoption plus historical GTK page registry | GTK page switching still owns existing widget creation and page list behavior | `LinuxUConsoleGtkPageRegistryAdoption -> GtkRuntimeEntryAdoption -> GtkUConsoleScreenGraphPresenter` | GTK page registry consumes descriptors as primary page source | contained fallback |
| LVGL hardcoded menu/page creation | `modules/ui_lvgl_ux_packs` compatibility runtime | device-specific LVGL renderers still own menu and screen creation | `LvglRuntimeAdoptionProbe -> LvglRuntimeEntryAdoption -> LvglRuntimeScreenGraphPresenter` | LVGL compatibility runtime consumes descriptors before creating menu/page objects | contained fallback |
| Chat LegacyDelivery bridges | `modules/ui_legacy_adapters` | chat delivery send/result ownership still has protocol compatibility bridges | chat delivery action/event ports and runtime projectors | `LegacyChatDeliveryActionBridge` and `LegacyChatDeliveryEventBridge` have no main runtime callers | contained fallback |
| KeyVerification legacy source/sink | `modules/ui_legacy_adapters` | key verification still crosses protocol/session compatibility surfaces | key verification presentation source and action sink ports | protocol-specific adapters are renamed or split into stable non-legacy owners | contained fallback |
| MapOverlay legacy source | `modules/ui_legacy_adapters` | map overlay rendering still carries compatibility projection from older GPS/team paths | `MapOverlaySnapshot` and stable map overlay projector/source | renderers consume structured overlay snapshots without legacy concrete source ownership | contained fallback |
