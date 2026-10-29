include("${CMAKE_CURRENT_LIST_DIR}/../ESP_IDF_COMPONENT_SOURCES.cmake")

# GCC 14's O2 inlining cannot prove std::sort's bounded 8-entry range here.
# Keep the diagnostic, but do not make this specific false positive fatal on P4.
if(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_tft" OR
   TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_amoled")
    set_source_files_properties(
        "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-error=array-bounds")
    # copyHost() writes an explicit NUL after its bounded strncpy call.
    set_source_files_properties(
        "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/chat/infra/reticulum/reticulum_interfaces.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-error=stringop-truncation")
    # Every msgpack append checks `used` against the caller's 96-byte capacity.
    set_source_files_properties(
        "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_adapter.cpp"
        PROPERTIES COMPILE_OPTIONS "-Wno-error=array-bounds")
endif()

set(TRAILMATE_ESP_IDF_PLATFORM_SOURCES
    ${TRAILMATE_ESP_IDF_PLATFORM_COMMON_SOURCES})

set(TRAILMATE_ESP_IDF_UI_SOURCES
    ${TRAILMATE_ESP_IDF_UI_SHARED_SOURCES})

if(TRAIL_MATE_IDF_TARGET STREQUAL "tdeck")
    list(APPEND TRAILMATE_ESP_IDF_UI_SOURCES
        ${TRAILMATE_ESP_IDF_TDECK_PRO_UI_SOURCES}
        ${TRAILMATE_ESP_IDF_MONO_240X320_UI_SOURCES})
endif()

if(TRAIL_MATE_IDF_TARGET STREQUAL "tab5")
    list(APPEND TRAILMATE_ESP_IDF_PLATFORM_SOURCES
        ${TRAILMATE_ESP_IDF_TAB5_BOARD_SOURCES})
elseif(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_tft" OR TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_amoled")
    list(APPEND TRAILMATE_ESP_IDF_PLATFORM_SOURCES
        ${TRAILMATE_ESP_IDF_T_DISPLAY_P4_BOARD_SOURCES})
endif()

idf_component_register(
    SRCS
        "idf_entry.cpp"
        ${TRAILMATE_ESP_IDF_APP_SHELL_SOURCES}
        ${TRAILMATE_ESP_IDF_PRODUCT_COMPOSITION_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_HOSTLINK_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_SYS_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_MUSIC_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_SSTV_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_WALKIE_SOURCES}
        ${TRAILMATE_ESP_IDF_AGENDA_WIDGET_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_DEVICE_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_GPS_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_CHAT_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_TEAM_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_MESH_SOURCES}
        ${TRAILMATE_ESP_IDF_CORE_PHONE_SOURCES}
        ${TRAILMATE_ESP_IDF_UI_SOURCES}
        ${TRAILMATE_ESP_IDF_UI_PRESENTATION_SOURCES}
        ${TRAILMATE_ESP_IDF_UI_LVGL_UX_PACK_SOURCES}
        ${TRAILMATE_ESP_IDF_PLATFORM_SOURCES}
    INCLUDE_DIRS
        ${TRAILMATE_ESP_IDF_FINAL_INCLUDE_DIRS}
    REQUIRES
        app_update
        driver
        esp_app_format
        esp_codec_dev
        esp_lcd
        esp_lvgl_port
        esp_serial_slave_link
        esp_tinyusb
        esp_timer
        espcoredump
        fatfs
        json
        m5stack_tab5
        mbedtls
        nvs_flash
        sensor_bmi270
        t_display_p4
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22
    TRAIL_MATE_SDFAT_SDMMC=1
    SDFAT_FILE_TYPE=3
    USE_BLOCK_DEVICE_INTERFACE=1
    ENABLE_ARDUINO_FEATURES=0
    ENABLE_ARDUINO_SERIAL=0
    ENABLE_ARDUINO_STRING=0
    USE_FCNTL_H=1)

if(TRAIL_MATE_IDF_TARGET STREQUAL "tab5")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE TRAIL_MATE_ESP_BOARD_TAB5=1)
elseif(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_tft")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1)
elseif(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_amoled")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1
        TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4_AMOLED=1)
endif()

if(TRAIL_MATE_IDF_TARGET STREQUAL "tdeck")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TRAIL_MATE_MONO_SCREEN_240X320=1)
endif()

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
if(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_tft" OR
   TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_amoled")
    # LVGL introduces these references after libmain's archive scan. Keep the
    # custom allocator object reachable regardless of component archive order.
    target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--undefined=lv_malloc_core")
endif()
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    include("${TRAILMATE_ROOT}/cmake/TrailMateTmap.cmake")
    trailmate_add_tmap()
    target_link_libraries(${COMPONENT_LIB} PUBLIC tmap::tmap)
endif()
