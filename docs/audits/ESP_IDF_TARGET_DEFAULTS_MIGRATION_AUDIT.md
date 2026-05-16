# ESP-IDF Target Defaults Migration Audit

This audit records only target defaults that already exist in the repository.
No target configuration is invented in this batch.

| Target | Current location | Final location | Status | Delete condition |
| --- | --- | --- | --- | --- |
| `tab5` | `legacy/app_implementations/esp_idf/targets/tab5/sdkconfig.defaults` | `builds/esp_idf/targets/tab5/sdkconfig.defaults` | migrated | historical root no longer owns ESP-IDF component registration |
| `tdisplayp4_tft` | `legacy/app_implementations/esp_idf/targets/t_display_p4_tft/sdkconfig.defaults` | `builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults` | migrated | historical root no longer owns ESP-IDF component registration |
| `tdisplayp4_amoled` | `legacy/app_implementations/esp_idf/targets/t_display_p4_amoled/sdkconfig.defaults` | `builds/esp_idf/targets/tdisplayp4_amoled/sdkconfig.defaults` | migrated | historical root no longer owns ESP-IDF component registration |
| `tdeck` | none found in `legacy/app_implementations/esp_idf/targets` | `builds/esp_idf/targets/tdeck/sdkconfig.defaults` | missing / deferred | add only when repo evidence or product decision exists |
| `tlora_pager` | none found in `legacy/app_implementations/esp_idf/targets` | `builds/esp_idf/targets/tlora_pager/sdkconfig.defaults` | missing / deferred | add only when repo evidence or product decision exists |
| `twatch` | none found in `legacy/app_implementations/esp_idf/targets` | `builds/esp_idf/targets/twatch/sdkconfig.defaults` | missing / deferred | add only when repo evidence or product decision exists |

Current owner:

- `builds/esp_idf/targets`

Historical root status:

- `legacy/app_implementations/esp_idf/targets` may remain until the historical
  component root is deleted, but it is no longer the only owner for migrated
  target defaults.
