# `apps/esp_idf/targets/t_display_p4`

This directory is now a family note, not a buildable target.

`LILYGO T-Display-P4` exists in two real display/touch hardware variants, so the old generic
`TRAIL_MATE_IDF_TARGET=t_display_p4` target was retired in favor of two explicit environments:

- `TRAIL_MATE_IDF_TARGET=t_display_p4_tft`
- `TRAIL_MATE_IDF_TARGET=t_display_p4_amoled`

Use the target that matches the physical module on your board:

- TFT variant:
  [t_display_p4_tft](../t_display_p4_tft/README.md)
- AMOLED variant:
  [t_display_p4_amoled](../t_display_p4_amoled/README.md)

Shared ownership stays the same:

- `boards/t_display_p4/*`
  Own shared board-family truth and hardware arbitration.
- `platform/esp/idf_components/t_display_p4/*`
  Own shared P4-side display/touch/LVGL runtime.
- `platform/esp/idf_common/*`
  Own shared glue only.

The repo still builds only the ESP32-P4 firmware. The ESP32-C6 companion firmware remains an external flashing contract.
