# ESP-IDF Target Defaults Migration Audit

This audit records migrated ESP-IDF target defaults and the current final owner
for targets where no historical ESP-IDF defaults existed. Compile baselines are
derived from repository board metadata and must not be treated as hardware
validation.

| Target | Current location | Final location | Status | Delete condition |
| --- | --- | --- | --- | --- |
| `tab5` | `legacy/app_implementations/esp_idf/targets/tab5/sdkconfig.defaults` | `builds/esp_idf/targets/tab5/sdkconfig.defaults` | migrated | historical root no longer owns ESP-IDF component registration |
| `tdisplayp4_tft` | `legacy/app_implementations/esp_idf/targets/t_display_p4_tft/sdkconfig.defaults` | `builds/esp_idf/targets/tdisplayp4_tft/sdkconfig.defaults` | migrated | historical root no longer owns ESP-IDF component registration |
| `tdisplayp4_amoled` | `legacy/app_implementations/esp_idf/targets/t_display_p4_amoled/sdkconfig.defaults` | `builds/esp_idf/targets/tdisplayp4_amoled/sdkconfig.defaults` | migrated | historical root no longer owns ESP-IDF component registration |
| `tdeck` | none found in `legacy/app_implementations/esp_idf/targets` | `builds/esp_idf/targets/tdeck/sdkconfig.defaults` | compile baseline from `boards/T-Deck.json`; hardware validation pending | replace baseline with validated ESP-IDF hardware defaults when target is exercised |
| `tlora_pager` | none found in `legacy/app_implementations/esp_idf/targets` | `builds/esp_idf/targets/tlora_pager/sdkconfig.defaults` | compile baseline from `boards/lilygo-t-lora-pager.json`; hardware validation pending | replace baseline with validated ESP-IDF hardware defaults when target is exercised |
| `twatch` | none found in `legacy/app_implementations/esp_idf/targets` | `builds/esp_idf/targets/twatch/sdkconfig.defaults` | compile baseline from `boards/lilygo-t-watch-s3.json`; hardware validation pending | replace baseline with validated ESP-IDF hardware defaults when target is exercised |

Current owner:

- `builds/esp_idf/targets`

Historical root status:

- `legacy/app_implementations/esp_idf/targets` may remain until the historical
  component root is deleted, but it is no longer the only owner for migrated
  target defaults.
