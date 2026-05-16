# Target Matrix

This matrix records Batch 3 final target architecture ownership. It separates
final target intent from the currently executable fallback paths already present
in this repository.

No missing target defaults, widget trees, or hardware facts are invented here.
Rows marked `PendingHardwareValidation` have final owners assigned, but still
need build or hardware evidence before they can be called complete product
routes.

| target_id | board_id | build_entrypoint | app_shell | platform | renderer | ux_pack_id | active_ux_pack | ui_profile_id | page_manifest_id | layout_profile_id | support_status | known_fallback | owner |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tab5` | `tab5` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `tab5_touch` | `compatibility` | `tab5_touch_ui` | `tab5_touch_manifest` | `tab5_large_touch` | `ActiveWithFallback` | current executable UX pack is `compatibility` | `apps/esp32_lvgl + builds/esp_idf + boards/tab5` |
| `tdisplayp4_tft` | `tdisplayp4` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `tdisplayp4_touch` | `compatibility` | `tdisplayp4_touch_ui` | `tdisplayp4_touch_manifest` | `tdisplayp4_touch` | `ActiveWithFallback` | current executable UX pack is `compatibility` | `apps/esp32_lvgl + builds/esp_idf + boards/tdisplayp4` |
| `tdisplayp4_amoled` | `tdisplayp4` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `tdisplayp4_touch` | `compatibility` | `tdisplayp4_touch_ui` | `tdisplayp4_touch_manifest` | `tdisplayp4_touch` | `ActiveWithFallback` | current executable UX pack is `compatibility` | `apps/esp32_lvgl + builds/esp_idf + boards/tdisplayp4` |
| `tlora_pager` | `tlora_pager` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `pager_compact` | `compatibility` | `pager_compact_ui` | `pager_compact_manifest` | `pager_compact` | `PendingHardwareValidation` | existing target capability still records Arduino/PIO evidence | `apps/esp32_lvgl + builds/esp_idf + boards/tlora_pager` |
| `tdeck` | `tdeck` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `deck_full` | `compatibility` | `deck_wide_ui` | `deck_full_manifest` | `deck_wide` | `PendingHardwareValidation` | existing target capability still records Arduino/PIO evidence | `apps/esp32_lvgl + builds/esp_idf + boards/tdeck` |
| `twatch` | `twatch` | `builds/esp_idf` | `apps/esp32_lvgl` | ESP-IDF | LVGL | `watch_compact` | `compatibility` | `watch_compact_ui` | `watch_compact_manifest` | `watch_compact` | `PendingHardwareValidation` | current repo evidence is board and variant data, not migrated IDF defaults | `apps/esp32_lvgl + builds/esp_idf + boards/twatch` |
| `uconsole` | `uconsole` | `builds/linux_cmake` | `apps/linux_uconsole_gtk` | Linux | GTK | `uconsole_desktop` | `uconsole_desktop` | `uconsole_desktop_ui` | `uconsole_desktop_manifest` | `uconsole_desktop` | `Active` | none for UX pack selection | `apps/linux_uconsole_gtk + builds/linux_cmake + boards/uconsole` |
| `cardputerzero` | `cardputerzero` | `builds/linux_cmake` | `apps/linux_sim_shell` | Linux | ASCII | `cardputer_compact` | `simulator_full` | `cardputer_compact_ui` | `cardputer_compact_manifest` | `cardputer_compact` | `ActiveWithFallback` | dedicated Linux device shell remains deferred by current repo evidence | `apps/linux_sim_shell + builds/linux_cmake + boards/cardputerzero` |
| `gat562_mesh_evb_pro` | `gat562_mesh_evb_pro` | `builds/pio_nrf52` | `apps/nrf52_node` | PlatformIO | Headless | `node_headless` | `tiny_node_status` | `node_headless_ui` | `node_headless_manifest` | `headless_node` | `Headless` | current executable UX pack is `tiny_node_status`; board hardware still records a 128 x 64 display | `apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro` |

## Pending Evidence

| target_id | pending evidence | Final owner |
| --- | --- | --- |
| `tlora_pager` | ESP-IDF target defaults and hardware validation for the requested IDF route | `builds/esp_idf + boards/tlora_pager` |
| `tdeck` | ESP-IDF target defaults and hardware validation for the requested IDF route | `builds/esp_idf + boards/tdeck` |
| `twatch` | ESP-IDF target defaults and hardware validation for the requested IDF route | `builds/esp_idf + boards/twatch` |
| `cardputerzero` | final dedicated Linux device app shell decision; current repo evidence points to Linux CMake plus simulator fallback | `apps/linux_sim_shell` until a proven dedicated shell exists |

## Batch 2 Status Tokens

The former Batch 2 statuses `migrated_foundation` and
`pending_final_profile` are historical labels. Batch 3 replaces them with
`Active`, `ActiveWithFallback`, `PendingHardwareValidation`, and `Headless`.
