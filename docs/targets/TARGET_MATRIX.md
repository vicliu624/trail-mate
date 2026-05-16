# Target Matrix

This matrix records the current final-owner foundation. Batch 2 covers the
ESP-IDF/LVGL targets whose target defaults already exist in this repository.
No missing target configuration is invented here.

## Migrated Foundation Targets

| Target | Board owner | Build entrypoint | App shell | Renderer | UX pack | Status |
| --- | --- | --- | --- | --- | --- | --- |
| `tab5` | `boards/tab5` | `builds/esp_idf` | `apps/esp32_lvgl` | LVGL | `compatibility` | migrated_foundation |
| `tdisplayp4_tft` | `boards/t_display_p4` | `builds/esp_idf` | `apps/esp32_lvgl` | LVGL | `compatibility` | migrated_foundation |
| `tdisplayp4_amoled` | `boards/t_display_p4` | `builds/esp_idf` | `apps/esp32_lvgl` | LVGL | `compatibility` | migrated_foundation |

## Pending Targets

| Target | Current owner evidence | Final owner | Status |
| --- | --- | --- | --- |
| `tdeck` | `docs/targets/esp-t-deck-lvgl.capabilities.yaml` | `apps/esp32_lvgl + builds/esp_idf + boards/<target>` after board/defaults evidence is established | pending_final_profile |
| `tlora_pager` | `docs/targets/esp-t-lora-pager-lvgl.capabilities.yaml` | `apps/esp32_lvgl + builds/esp_idf + boards/<target>` after board/defaults evidence is established | pending_final_profile |
| `twatch` | no ESP-IDF target defaults found in this batch | `apps/esp32_lvgl + builds/esp_idf + boards/<target>` after repo evidence is established | pending_final_profile |
| `uconsole` | `apps/linux_uconsole_gtk` | `apps/linux_uconsole_gtk + builds/linux_cmake + boards/uconsole` | pending_final_profile |
| `cardputerzero` | no final shell decision in this batch | final app/build owner after product route decision | pending_final_profile |
| `gat562_mesh_evb_pro` | `apps/nrf52_node`, `builds/pio_nrf52`, `boards/gat562_mesh_evb_pro` | `apps/nrf52_node + builds/pio_nrf52 + boards/gat562_mesh_evb_pro` | pending_final_profile |

Rules:

- target defaults live under `builds/esp_idf/targets` when migrated;
- app shells do not compile legacy implementation adapters;
- pending rows are not permission to invent board facts or UI manifests.
