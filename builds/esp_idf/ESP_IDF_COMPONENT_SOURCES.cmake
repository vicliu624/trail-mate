# Final ESP-IDF component source ownership for migrated Batch 2 surfaces.
#
# The physical ESP-IDF component may temporarily include this file from the
# legacy component root while component registration is being moved. The source
# list owner is this build entrypoint plus the final app/platform/module owners.

set(TRAILMATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(TRAILMATE_ESP_IDF_APP_SHELL_SOURCES
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_historical_source_descriptor.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_loop_runtime.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_runtime_config.cpp")

set(TRAILMATE_ESP_IDF_PRODUCT_COMPOSITION_SOURCES
    "${TRAILMATE_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${TRAILMATE_ROOT}/modules/product_composition/src/target_build_binding.cpp")

set(TRAILMATE_ESP_IDF_PLATFORM_SOURCES
    "${TRAILMATE_ROOT}/platform/esp/radio/meshtastic_radio_adapter.cpp")

set(TRAILMATE_ESP_IDF_FINAL_INCLUDE_DIRS
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src"
    "${TRAILMATE_ROOT}/modules/product_composition/include"
    "${TRAILMATE_ROOT}")
