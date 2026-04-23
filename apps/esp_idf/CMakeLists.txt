#Shared ESP - IDF application component.
#This component builds the migrated shared modules plus the shared IDF app shell.

file(GLOB_RECURSE trail_mate_idf_local_sources
    ${CMAKE_CURRENT_LIST_DIR}/src/*.c
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cc
    ${CMAKE_CURRENT_LIST_DIR}/src/*.cpp
)

file(GLOB_RECURSE trail_mate_idf_external_sources
    ${CMAKE_SOURCE_DIR}/modules/core_sys/src/*.c
    ${CMAKE_SOURCE_DIR}/modules/core_sys/src/*.cc
    ${CMAKE_SOURCE_DIR}/modules/core_sys/src/*.cpp
    ${CMAKE_SOURCE_DIR}/modules/core_chat/src/*.c
    ${CMAKE_SOURCE_DIR}/modules/core_chat/src/*.cc
    ${CMAKE_SOURCE_DIR}/modules/core_chat/src/*.cpp
    ${CMAKE_SOURCE_DIR}/modules/core_chat/generated/*.c
    ${CMAKE_SOURCE_DIR}/modules/core_chat/generated/*.cc
    ${CMAKE_SOURCE_DIR}/modules/core_chat/generated/*.cpp
    ${CMAKE_SOURCE_DIR}/modules/core_chat/third_party/nanopb/*.c
    ${CMAKE_SOURCE_DIR}/modules/core_chat/third_party/nanopb/*.cc
    ${CMAKE_SOURCE_DIR}/modules/core_chat/third_party/nanopb/*.cpp
    ${CMAKE_SOURCE_DIR}/modules/core_gps/src/*.c
    ${CMAKE_SOURCE_DIR}/modules/core_gps/src/*.cc
    ${CMAKE_SOURCE_DIR}/modules/core_gps/src/*.cpp
    ${CMAKE_SOURCE_DIR}/modules/core_hostlink/src/*.c
    ${CMAKE_SOURCE_DIR}/modules/core_hostlink/src/*.cc
    ${CMAKE_SOURCE_DIR}/modules/core_hostlink/src/*.cpp
    ${CMAKE_SOURCE_DIR}/modules/core_team/src/*.c
    ${CMAKE_SOURCE_DIR}/modules/core_team/src/*.cc
    ${CMAKE_SOURCE_DIR}/modules/core_team/src/*.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/idf_common/src/*.c
    ${CMAKE_SOURCE_DIR}/platform/esp/idf_common/src/*.cc
    ${CMAKE_SOURCE_DIR}/platform/esp/idf_common/src/*.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/idf_common/src/gps_runtime.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/sys/event_bus.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/hostlink/hostlink_service.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/hostlink/hostlink_bridge_radio.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/hostlink/hostlink_config_service.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/sstv/decode_sstv.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/sstv/cordic.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/sstv/half_band_filter2.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/sstv/sstv_service.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/walkie/walkie_service.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/boards/src/board_runtime.cpp
)

include(${CMAKE_CURRENT_LIST_DIR}/../../third_party/codec2/codec2_sources.cmake)
list(APPEND trail_mate_idf_external_sources
    ${trail_mate_codec2_sources}
)

set(trail_mate_idf_requires
    esp_app_format
    esp_common
    esp_event
    esp_http_client
    esp_lcd
    esp_netif
    esp_timer
    json
    log
    lvgl__lvgl
    nvs_flash
    fatfs
    sdmmc
    esp_driver_gpio
    esp_driver_i2c
    esp_driver_i2s
    esp_driver_ledc
    esp_driver_spi
    esp_driver_uart
    esp_driver_usb_serial_jtag
    esp_wifi
    espressif__esp_codec_dev
    espressif__esp_lvgl_port
    mbedtls
)

set(trail_mate_idf_ui_shared_sources
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/app_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/app_catalog_builder.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/formatters.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/loop_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/startup_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/menu_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/ui_status.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/menu_profile.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/menu_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/ui_boot.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/widgets/top_bar.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/widgets/busy_overlay.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/widgets/system_notification.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/widgets/map/map_viewport.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/widgets/toast/toast_widget.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/widgets/ime/ime_widget.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/widgets/ime/pinyin_ime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/assets/fonts/font_utils.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/components/air_status_footer.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/components/info_card.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/components/two_pane_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/components/two_pane_styles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/components/two_pane_nav.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/presentation_contract.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/chat_compose_scaffold.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/chat_thread_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/boot_splash_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/directory_browser_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/directory_browser_nav.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/instrument_panel_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/map_focus_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/menu_dashboard_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/presentation_registry.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/service_panel_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/presentation/watch_face_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/theme/theme_slots.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/theme/theme_registry.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/page/page_profile.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/runtime/memory_profile.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/runtime/pack_repository.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/common/placeholder_page.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_compose_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_compose_input.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_compose_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_compose_styles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_conversation_input.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_conversation_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_conversation_styles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_message_list_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_message_list_input.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_message_list_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_message_list_styles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_protocol_support.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_send_flow.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/gps/gps_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_modal.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_components.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_input.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_layout.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_lifetime.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_route_overlay.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/screens/gps/gps_tracker_overlay.cpp
    ${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/extensions/extensions_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/extensions/extensions_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/sstv/sstv_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/pc_link/pc_link_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/pc_link/pc_link_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/usb/usb_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/usb/usb_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/settings/settings_page_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/settings/settings_page_styles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/settings/settings_page_input.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/settings/settings_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/settings/settings_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/settings/settings_state.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/contacts/contacts_page_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/contacts/contacts_page_input.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/contacts/contacts_page_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/contacts/contacts_page_styles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/contacts/contacts_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/contacts/contacts_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/contacts/contacts_state.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/node_info/node_info_page_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/tracker/tracker_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/tracker/tracker_page_runtime.cpp
    
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/tracker/tracker_page_input.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/tracker/tracker_page_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/tracker/tracker_state.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/team/team_page_input.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/team/team_page_layout.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/team/team_page_styles.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/team/team_page_components.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/team/team_state.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/team/team_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/team/team_page_runtime.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/walkie_talkie/walkie_talkie_page_shell.cpp
    ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/screens/walkie_talkie/walkie_talkie_page_runtime.cpp
)

set(trail_mate_idf_include_dirs
    "include"
    "${CMAKE_SOURCE_DIR}/platform/shared/include"
    "${CMAKE_SOURCE_DIR}/boards/tab5/include"
    "${CMAKE_SOURCE_DIR}/boards/t_display_p4/include"
    "${CMAKE_SOURCE_DIR}/modules/ui_shared/include"
    "${CMAKE_SOURCE_DIR}/modules/core_sys/include"
    "${CMAKE_SOURCE_DIR}/modules/core_chat/include"
    "${CMAKE_SOURCE_DIR}/modules/core_chat/generated"
    "${CMAKE_SOURCE_DIR}/modules/core_chat/third_party/nanopb"
    "${CMAKE_SOURCE_DIR}/modules/core_gps/include"
    "${CMAKE_SOURCE_DIR}/modules/core_hostlink/include"
    "${CMAKE_SOURCE_DIR}/modules/core_team/include"
    "${CMAKE_SOURCE_DIR}/platform/esp/idf_common/include"
    "${CMAKE_SOURCE_DIR}/platform/esp/arduino_common/include"
    "${CMAKE_SOURCE_DIR}/platform/esp/common/include"
    "${CMAKE_SOURCE_DIR}/third_party/codec2/src"
    "${CMAKE_SOURCE_DIR}/platform/esp/boards/include"
)

if(TRAIL_MATE_IDF_TARGET STREQUAL "tab5")
    set(trail_mate_dashboard_sources
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/menu_dashboard.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_state.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_style.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_mesh_widget.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_gps_widget.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_recent_widget.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_compass_widget.cpp
    )
    list(APPEND trail_mate_idf_include_dirs
        "${CMAKE_SOURCE_DIR}/platform/esp/idf_components/m5stack_tab5/include"
        "${CMAKE_SOURCE_DIR}/platform/esp/idf_components/sensor_bmi270/include"
    )
    list(APPEND trail_mate_idf_requires
        m5stack_tab5
        sensor_bmi270
    )
    list(APPEND trail_mate_idf_external_sources
        ${CMAKE_SOURCE_DIR}/boards/tab5/src/tab5_board.cpp
        ${CMAKE_SOURCE_DIR}/boards/tab5/src/rtc_runtime.cpp
        ${CMAKE_SOURCE_DIR}/boards/tab5/src/heading_runtime.cpp
        ${CMAKE_SOURCE_DIR}/boards/tab5/src/codec_compat.cpp
    )
    list(APPEND trail_mate_idf_ui_shared_sources
        ${trail_mate_dashboard_sources}
    )
elseif(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_tft" OR
       TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_amoled")
    set(trail_mate_dashboard_sources
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/menu_dashboard.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_state.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_style.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_mesh_widget.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_gps_widget.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_recent_widget.cpp
        ${CMAKE_SOURCE_DIR}/modules/ui_shared/src/ui/menu/dashboard/dashboard_compass_widget.cpp
    )
    list(APPEND trail_mate_idf_include_dirs
        "${CMAKE_SOURCE_DIR}/platform/esp/idf_components/t_display_p4/include"
    )
    list(APPEND trail_mate_idf_requires
        t_display_p4
    )
    list(APPEND trail_mate_idf_external_sources
        ${CMAKE_SOURCE_DIR}/boards/t_display_p4/src/t_display_p4_board.cpp
        ${CMAKE_SOURCE_DIR}/boards/t_display_p4/src/rtc_runtime.cpp
    )
    list(APPEND trail_mate_idf_ui_shared_sources
        ${trail_mate_dashboard_sources}
    )
endif()

list(FILTER trail_mate_idf_external_sources EXCLUDE REGEX ".*/modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers\.cpp$")
list(FILTER trail_mate_idf_external_sources EXCLUDE REGEX ".*/modules/core_chat/src/infra/meshtastic/mt_packet_wire\.cpp$")
list(FILTER trail_mate_idf_external_sources EXCLUDE REGEX ".*/modules/core_chat/src/infra/meshtastic/mt_pki_crypto\.cpp$")
list(FILTER trail_mate_idf_external_sources EXCLUDE REGEX ".*/modules/core_chat/src/ble/.*\.cpp$")
list(FILTER trail_mate_idf_local_sources EXCLUDE REGEX ".*/src/idf_component_anchor\.cpp$")
list(FILTER trail_mate_idf_local_sources EXCLUDE REGEX ".*/src/gps_tracker_overlay\.cpp$")

idf_component_register(
    SRCS "src/idf_component_anchor.cpp"
    INCLUDE_DIRS
        ${trail_mate_idf_include_dirs}
    REQUIRES
        ${trail_mate_idf_requires}
)

target_sources(${COMPONENT_LIB} PRIVATE
    ${trail_mate_idf_local_sources}
    ${trail_mate_idf_external_sources}
    ${trail_mate_idf_ui_shared_sources}
)

if(TRAIL_MATE_IDF_TARGET STREQUAL "tab5")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TRAIL_MATE_ESP_BOARD_TAB5=1
        TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22
    )
elseif(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_tft")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1
        TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4_TFT=1
        TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22
    )
elseif(TRAIL_MATE_IDF_TARGET STREQUAL "t_display_p4_amoled")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1
        TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4_AMOLED=1
        TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22
    )
else()
    message(FATAL_ERROR "Unsupported TRAIL_MATE_IDF_TARGET='${TRAIL_MATE_IDF_TARGET}' inside apps/esp_idf/CMakeLists.txt")
endif()
