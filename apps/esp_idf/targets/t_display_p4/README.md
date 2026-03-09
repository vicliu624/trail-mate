# `apps/esp_idf/targets/t_display_p4`

ESP-IDF target descriptor for T-Display-P4.

Source reference for board bring-up:

- `.tmp/T-Display-P4`

This target now owns:

- T-Display-P4-specific `sdkconfig.defaults`
- future partition-table selection if the device needs its own flash layout
- target selection / board binding into `platform/esp/boards/t_display_p4`

Expected split:

- shared IDF app assembly -> `apps/esp_idf`
- shared IDF platform glue -> `platform/esp/idf_common`
- board-specific runtime/pins/capabilities -> `platform/esp/boards/t_display_p4`

Current wiring:

- selected by `TRAIL_MATE_IDF_TARGET=t_display_p4`
- built through the shared `apps/esp_idf` component root
- board-specific runtime selection is handled through `TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4`
