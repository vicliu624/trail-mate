# ESP-IDF Build Entrypoint

Authoritative ESP build entrypoint.

Build authority:

```text
ESP / ESP32-P4 -> ESP-IDF
```

Removed historical component root:

```text
esp_idf implementation root
```

Final wrapper direction:

```text
builds/esp_idf
  -> invokes apps/esp32_lvgl app shell
```

This directory now owns the migrated ESP-IDF source list and target defaults
introduced in Batch 2:

- `ESP_IDF_COMPONENT_SOURCES.cmake`
- `targets/tab5/sdkconfig.defaults`
- `targets/tdisplayp4_tft/sdkconfig.defaults`
- `targets/tdisplayp4_amoled/sdkconfig.defaults`

Root legacy source has been removed. Any future physical ESP-IDF
`idf_component_register` owner must live under final build/app/platform
ownership, not under a restored historical source root.

Rules:

- thin wrapper only
- Build Entrypoint invokes
- App Shell composes
- do not assemble Chat/Map/GPS runtime here
- do not choose UX pack here
- do not define board facts here
- do not invent target defaults that are not backed by repository evidence
