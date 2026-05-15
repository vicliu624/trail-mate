# `apps/esp_idf/targets/tab5`

ESP-IDF target descriptor for Tab5.

This target now owns:

- Tab5-specific `sdkconfig.defaults`
- future Tab5-specific partition choices if needed
- target selection / board binding into `platform/esp/boards/tab5`

It does not own generic IDF startup/runtime composition.
That shared logic lives in `apps/esp_idf` and `platform/esp/idf_common`.

Current wiring:

- selected by `TRAIL_MATE_IDF_TARGET=tab5`
- built through the shared `apps/esp_idf` component root
- board-specific runtime selection is handled through `TRAIL_MATE_ESP_BOARD_TAB5`
