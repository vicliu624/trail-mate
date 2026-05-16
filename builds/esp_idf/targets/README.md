# ESP-IDF Target Defaults

Final owner for ESP-IDF target `sdkconfig.defaults` files.

Migrated from the historical ESP-IDF component root:

| Final target id | Final location | Historical source |
| --- | --- | --- |
| `tab5` | `builds/esp_idf/targets/tab5/sdkconfig.defaults` | `legacy/app_implementations/esp_idf/targets/tab5/sdkconfig.defaults` |
| `tdisplayp4_tft` | `builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults` | `legacy/app_implementations/esp_idf/targets/t_display_p4_tft/sdkconfig.defaults` |
| `tdisplayp4_amoled` | `builds/esp_idf/targets/tdisplayp4_amoled/sdkconfig.defaults` | `legacy/app_implementations/esp_idf/targets/t_display_p4_amoled/sdkconfig.defaults` |

Deferred targets such as `tdeck`, `tlora_pager`, and `twatch` are not invented
here. They remain pending until the repository has target defaults to migrate or
an explicit target-defaults decision is recorded.
