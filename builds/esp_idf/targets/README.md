# ESP-IDF Target Defaults

Final owner for ESP-IDF target `sdkconfig.defaults` files.

Migrated from the historical ESP-IDF component root:

| Final target id | Final location | Historical source |
| --- | --- | --- |
| `tab5` | `builds/esp_idf/targets/tab5/sdkconfig.defaults` | `removed root esp_idf/targets/tab5/sdkconfig.defaults` |
| `tdisplayp4_tft` | `builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults` | `removed root esp_idf/targets/t_display_p4_tft/sdkconfig.defaults` |
| `tdisplayp4_amoled` | `builds/esp_idf/targets/tdisplayp4_amoled/sdkconfig.defaults` | `removed root esp_idf/targets/t_display_p4_amoled/sdkconfig.defaults` |
| `tdeck` | `builds/esp_idf/targets/tdeck/sdkconfig.defaults` | minimal compile baseline from `boards/T-Deck.json` |
| `tlora_pager` | `builds/esp_idf/targets/tlora_pager/sdkconfig.defaults` | minimal compile baseline from `boards/lilygo-t-lora-pager.json` |
| `twatch` | `builds/esp_idf/targets/twatch/sdkconfig.defaults` | minimal compile baseline from `boards/lilygo-t-watch-s3.json` |

`tdeck`, `tlora_pager`, and `twatch` now have final target-default owners under
`builds/esp_idf/targets`. Their current defaults are compile baselines derived
from repository board metadata; hardware-specific PSRAM, display, input, NFC,
and audio validation remains pending.
