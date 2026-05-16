# Target UX Pack Gap Report

Batch 3 records final desired UX pack IDs for every target, but this repository
currently has only these executable pack IDs:

- `compatibility`
- `uconsole_desktop`
- `tiny_node_status`
- `simulator_full`

The table below prevents missing final packs from being mistaken for completed
runtime implementations.

| target | desired ux_pack | current actual fallback | exit condition | owner |
| --- | --- | --- | --- | --- |
| `tab5` | `tab5_touch` | `compatibility` | executable `tab5_touch` pack registered and covered by app shell smoke | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `tdisplayp4_tft` | `tdisplayp4_touch` | `compatibility` | executable `tdisplayp4_touch` pack registered for TFT panel route | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `tdisplayp4_amoled` | `tdisplayp4_touch` | `compatibility` | executable `tdisplayp4_touch` pack registered for AMOLED panel route | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `tlora_pager` | `pager_compact` | `compatibility` | executable `pager_compact` pack registered after IDF route evidence is closed | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `tdeck` | `deck_full` | `compatibility` | executable `deck_full` pack registered after IDF route evidence is closed | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `twatch` | `watch_compact` | `compatibility` | executable `watch_compact` pack registered after IDF route evidence is closed | `modules/ui_lvgl_ux_packs + apps/esp32_lvgl` |
| `uconsole` | `uconsole_desktop` | `uconsole_desktop` | none; current pack already resolves | `apps/linux_uconsole_gtk + modules/ui_gtk_runtime` |
| `cardputerzero` | `cardputer_compact` | `simulator_full` | final Cardputer Zero Linux device app shell or accepted simulator-shell route consumes `cardputer_compact` | `apps/linux_sim_shell + platform/linux/common` |
| `gat562_mesh_evb_pro` | `node_headless` | `tiny_node_status` | headless descriptor consumer becomes the active nRF52 node path | `apps/nrf52_node + modules/ui_headless_runtime` |

## Rule

`TargetProfile.ux_pack_id` is final intent. `TargetUxBinding.active_ux_pack_id`
is the currently resolvable runtime pack. App shells must use the binding rather
than choose a fallback locally.
