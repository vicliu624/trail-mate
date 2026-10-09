cmake_minimum_required(VERSION 3.20)
project(TrailMateWebSimulator LANGUAGES C CXX)
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "Configure with the Emscripten toolchain")
endif()
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE STRING "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LVGL_SOURCE_DIR "${REPO_ROOT}/.pio/libdeps/tlora_pager_sx1262/lvgl" CACHE PATH "LVGL 9.4 firmware checkout")
if(NOT EXISTS "${LVGL_SOURCE_DIR}/CMakeLists.txt")
  include(FetchContent)
  FetchContent_Declare(lvgl_source GIT_REPOSITORY https://github.com/lvgl/lvgl.git GIT_TAG v9.4.0 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(lvgl_source)
else()
  add_subdirectory("${LVGL_SOURCE_DIR}" lvgl)
endif()
set(NANOPB_SOURCE_DIR "${REPO_ROOT}/.pio/libdeps/tlora_pager_sx1262/Nanopb" CACHE PATH "Nanopb source")
if(NOT EXISTS "${NANOPB_SOURCE_DIR}/pb.h")
  include(FetchContent)
  FetchContent_Declare(nanopb_source GIT_REPOSITORY https://github.com/nanopb/nanopb.git GIT_TAG nanopb-0.4.9.1 GIT_SHALLOW TRUE SOURCE_SUBDIR preview-download-only)
  FetchContent_MakeAvailable(nanopb_source)
  set(NANOPB_SOURCE_DIR "${nanopb_source_SOURCE_DIR}")
endif()
set(UI "${REPO_ROOT}/modules/ui_shared/src/ui")
function(add_native_preview PREVIEW_TARGET BOARD_DEFINITION OUTPUT_NAME)
add_executable(${PREVIEW_TARGET}
  native_runtime.cpp
  "${UI}/screens/common/placeholder_page.cpp"
  native_pages.cpp
  native_menu.cpp
  native_chat.cpp
  preview_platform.cpp
  preview_sensors.cpp
  preview_facade.cpp
  preview_status.cpp
  preview_extensions.cpp
  preview_sstv.cpp
  preview_settings.cpp
  preview_reticulum.cpp
  preview_radio.cpp
  preview_board.cpp
  "${UI}/app_catalog_builder.cpp"
  "${UI}/screens/geocaching/geocaching_page_shell.cpp"
  "${UI}/screens/calculator/calculator_page_shell.cpp"
  "${UI}/screens/gnss/gnss_skyplot_page_shell.cpp"
  "${UI}/screens/gps/gps_page_shell.cpp"
  "${UI}/screens/usb/usb_page_shell.cpp"
  "${UI}/screens/sstv/sstv_page_shell.cpp"
  "${UI}/screens/extensions/extensions_page_shell.cpp"
  "${UI}/screens/energy_sweep/energy_sweep_page_shell.cpp"
  "${UI}/screens/energy_sweep/energy_sweep_page_runtime.cpp"
  "${UI}/screens/sstv/sstv_page_runtime.cpp"
  "${UI}/screens/extensions/extensions_page_runtime.cpp"
  preview_map_tiles.cpp
  "${UI}/widgets/map/map_viewport.cpp"
  "${REPO_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_geometry.cpp"
  "${REPO_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp"
  "${UI}/menu/menu_layout.cpp"
  "${UI}/menu/menu_profile.cpp"
  "${UI}/menu/menu_runtime.cpp"
  "${UI}/ui_status.cpp"
  "${UI}/components/shortcut_help_modal.cpp"
  "${UI}/presentation_sources/runtime_gps_status_source.cpp"
  "${REPO_ROOT}/modules/ui_presentation/src/gps/gps_status_model.cpp"
  "${REPO_ROOT}/modules/ui_presentation/src/menu/menu_model.cpp"
  "${UI}/screens/gnss/gnss_skyplot_page_runtime.cpp"
  "${UI}/screens/usb/usb_page_runtime.cpp"
  "${UI}/screens/walkie_talkie/walkie_talkie_page_runtime.cpp"
  "${UI}/screens/walkie_talkie/walkie_talkie_page_shell.cpp"
  "${UI}/screens/calculator/calculator_page_runtime.cpp"
  "${UI}/screens/calculator/calculator_page_layout.cpp"
  "${UI}/screens/calculator/calculator_engine.cpp"
  "${UI}/widgets/top_bar.cpp"
  "${UI}/formatters.cpp"
  "${UI}/widgets/top_bar_power_presenter.cpp"
  "${UI}/widgets/text_candidate_data.cpp"
  "${UI}/page/page_profile.cpp"
  "${UI}/assets/fonts/font_utils.cpp"
  "${UI}/i18n/resource_pack_registry.cpp"
  "${UI}/runtime/memory_profile.cpp"
  "${UI}/runtime/ui_feedback.cpp"
  "${UI}/widgets/system_notification.cpp"
  "${UI}/assets/alert.c"
  "${REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
target_sources(${PREVIEW_TARGET} PRIVATE
  "${REPO_ROOT}/modules/core_sys/src/app/app_facade_access.cpp"
  "${REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
  "${REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
  "${REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
  "${REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
  "${REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
  "${REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
  "${REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
  "${REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
  "${REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp")
target_include_directories(${PREVIEW_TARGET} PRIVATE
  "${CMAKE_CURRENT_LIST_DIR}/platform_adapters"
  "${REPO_ROOT}/platform/shared/include"
  "${REPO_ROOT}/modules/core_team/include"
  "${REPO_ROOT}/modules/ui_chat_runtime/include"
  "${REPO_ROOT}/modules/ui_gps_runtime/include"
  "${REPO_ROOT}/modules/ui_key_verification_runtime/include"
  "${REPO_ROOT}/modules/chat_presentation_adapters/include"
  "${REPO_ROOT}/platform/linux/common/include"
  "${REPO_ROOT}/modules/ui_map_runtime/include"
  "${REPO_ROOT}/modules/ui_lvgl_ux_packs/include"
  "${REPO_ROOT}/modules/core_chat/generated"
  "${NANOPB_SOURCE_DIR}"
  "${REPO_ROOT}/modules/ui_shared/include"
  "${REPO_ROOT}/modules/ui_lvgl_core/include"
  "${REPO_ROOT}/modules/ui_presentation/include"
  "${REPO_ROOT}/modules/core_sys/include"
  "${REPO_ROOT}/modules/core_chat/include"
  "${REPO_ROOT}/modules/core_gps/include"
  "${REPO_ROOT}/platform/esp/arduino_common/include")
target_compile_features(${PREVIEW_TARGET} PRIVATE cxx_std_17)
target_compile_options(${PREVIEW_TARGET} PRIVATE -O2)
target_compile_definitions(${PREVIEW_TARGET} PRIVATE LV_CONF_INCLUDE_SIMPLE TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22 ${BOARD_DEFINITION}=1)
target_link_libraries(${PREVIEW_TARGET} PRIVATE lvgl)
file(GLOB PREVIEW_ASSETS "${UI}/assets/*.c")
target_sources(${PREVIEW_TARGET} PRIVATE ${PREVIEW_ASSETS})
file(GLOB CHAT_VIEW_SOURCES
  "${UI}/screens/chat/chat_message_list_*.cpp"
  "${UI}/screens/chat/chat_conversation_*.cpp"
  "${UI}/screens/chat/chat_compose_*.cpp")
target_sources(${PREVIEW_TARGET} PRIVATE ${CHAT_VIEW_SOURCES}
  "${UI}/screens/chat/chat_protocol_support.cpp"
  "${UI}/chat_voice_runtime.cpp"
  "${UI}/components/info_card.cpp"
  "${UI}/components/two_pane_styles.cpp"
  "${UI}/components/two_pane_layout.cpp"
  "${UI}/components/two_pane_nav.cpp"
  "${UI}/components/floating_search_box.cpp"
  "${UI}/components/map_poi_search.cpp"
  "${UI}/components/air_status_footer.cpp"
  "${UI}/widgets/text_candidate_picker.cpp"
  "${UI}/widgets/ime/ime_widget.cpp"
  "${UI}/widgets/ime/pinyin_ime.cpp"
  "${UI}/widgets/ime/ime_input_mode_descriptor.cpp"
  "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/common/touch_text_editor.cpp"
  "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/common/input_layout.cpp"
  "${UI}/presentation_sources/runtime_device_status_source.cpp"
  "${REPO_ROOT}/modules/ui_presentation/src/device/device_status_model.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshcore/mc_region_presets.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_radio_config.cpp")
target_sources(${PREVIEW_TARGET} PRIVATE "${REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_region.cpp")
set_source_files_properties("${REPO_ROOT}/modules/core_chat/src/infra/reticulum/reticulum_wire.cpp" PROPERTIES COMPILE_DEFINITIONS "TRAIL_MATE_RETICULUM_PARSE_ONLY=1")
file(GLOB_RECURSE CORE_TEAM_SOURCES "${REPO_ROOT}/modules/core_team/src/*.cpp")
file(GLOB EXTRA_VIEW_SOURCES
  "${UI}/screens/contacts/*.cpp"
  "${UI}/screens/node_info/*.cpp"
  "${UI}/screens/team/*.cpp"
  "${UI}/screens/settings/*.cpp"
  "${UI}/screens/tracker/*.cpp"
  "${UI}/team_actions/*.cpp"
  "${UI}/team_presentation/*.cpp")
target_sources(${PREVIEW_TARGET} PRIVATE ${CORE_TEAM_SOURCES} ${EXTRA_VIEW_SOURCES}
  "${UI}/screens/network/network_page_shell.cpp"
  "${UI}/screens/network/micron_markup_contract.cpp"
  "${UI}/screens/gps/gps_page_runtime.cpp"
  "${UI}/widgets/route_elevation_profile.cpp"
  "${UI}/widgets/route_image_strip.cpp"
  "${UI}/widgets/route_image_operation_presenter.cpp"
  "${UI}/widgets/busy_overlay.cpp"
  "${UI}/widgets/reticulum_ping_overlay.cpp"
  "${UI}/presentation_sources/runtime_settings_source.cpp"
  "${UI}/presentation_sources/team_chat_presentation_source.cpp"
  "${REPO_ROOT}/modules/ui_presentation/src/settings/settings_model.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/reticulum/reticulum_wire.cpp"
  "${REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
  "${REPO_ROOT}/platform/linux/common/src/platform/ui/tracker_runtime.cpp"
  "${REPO_ROOT}/platform/linux/common/src/platform/ui/route_storage.cpp"
  "${REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
  "${REPO_ROOT}/platform/linux/common/src/platform/ui/firmware_update_runtime.cpp"
  "${REPO_ROOT}/platform/linux/common/src/platform/ui/wireless_companion_runtime.cpp")
target_sources(${PREVIEW_TARGET} PRIVATE
  "${REPO_ROOT}/platform/linux/common/src/platform/ui/time_runtime.cpp"
  "${REPO_ROOT}/platform/linux/common/src/platform/ui/screen_runtime.cpp"
  "${REPO_ROOT}/modules/core_sys/src/platform/ui/timezone_profile.cpp"
  "${REPO_ROOT}/modules/core_sys/src/platform/ui/screen_power_state_machine.cpp"
  "${UI}/team_presence/team_presence_model.cpp"
  "${UI}/menu/dashboard/dashboard_style.cpp"
  "${UI}/presentation_sources/runtime_map_workspace_source.cpp"
  "${UI}/presentation_sources/team_map_overlay_source.cpp"
  "${REPO_ROOT}/modules/ui_presentation/src/map/map_workspace_model.cpp"
  "${REPO_ROOT}/modules/ui_gps_runtime/src/gps_page_runtime_pump.cpp"
  "${REPO_ROOT}/modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp"
  "${REPO_ROOT}/modules/ui_map_runtime/src/map_overlay_projection_adapter.cpp"
  "${REPO_ROOT}/modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp")
set_source_files_properties("${UI}/app_catalog_builder.cpp" PROPERTIES COMPILE_DEFINITIONS "ESP_PLATFORM=1")
target_link_options(${PREVIEW_TARGET} PRIVATE
  "-O0" "--no-entry" "-Wl,--error-limit=0" "-sMODULARIZE=1" "-sEXPORT_ES6=1"
  "-sEXPORT_NAME=createTrailMate" "-sALLOW_MEMORY_GROWTH=1"
  "-sSTACK_SIZE=262144" "-sENVIRONMENT=web,node"
  "SHELL:--preload-file \"${CMAKE_CURRENT_LIST_DIR}/data@/\""
  "SHELL:--preload-file \"${REPO_ROOT}/docs/reticulum/pages/trail-mate/index.mu@/pages/index.mu\""
  "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','ccall','HEAPU8']")
set_target_properties(${PREVIEW_TARGET} PROPERTIES SUFFIX ".js" OUTPUT_NAME "${OUTPUT_NAME}"
  RUNTIME_OUTPUT_DIRECTORY "${REPO_ROOT}/site/assets/native")
file(GLOB_RECURSE PREVIEW_DATA_FILES CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/data/*")
set_property(TARGET ${PREVIEW_TARGET} APPEND PROPERTY LINK_DEPENDS ${PREVIEW_DATA_FILES} "${REPO_ROOT}/docs/reticulum/pages/trail-mate/index.mu")
set_source_files_properties(
  "${UI}/screens/usb/usb_page_runtime.cpp"
  "${UI}/screens/walkie_talkie/walkie_talkie_page_runtime.cpp"
  "${UI}/screens/sstv/sstv_page_runtime.cpp"
  "${UI}/screens/energy_sweep/energy_sweep_page_runtime.cpp"
  PROPERTIES COMPILE_DEFINITIONS "ARDUINO=10819")
file(GLOB PROBE_PROTO_SOURCES "${REPO_ROOT}/modules/core_chat/generated/meshtastic/*.cpp")
file(GLOB PROBE_ED25519_SOURCES "${REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/*.c")
target_sources(${PREVIEW_TARGET} PRIVATE ${PROBE_PROTO_SOURCES} ${PROBE_ED25519_SOURCES}
  "${NANOPB_SOURCE_DIR}/pb_common.c"
  "${NANOPB_SOURCE_DIR}/pb_decode.c"
  "${NANOPB_SOURCE_DIR}/pb_encode.c"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_identity_crypto.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_payload_helpers.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_aes_ctr.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
  "${REPO_ROOT}/modules/core_chat/src/infra/rnode/rnode_packet_wire.cpp")
set_source_files_properties("${REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_aes_ctr.cpp" PROPERTIES COMPILE_DEFINITIONS "TRAILMATE_MESHTASTIC_FORCE_SOFTWARE_AES=1")


endfunction()
add_native_preview(trailmate_pager ARDUINO_T_LORA_PAGER pager)
add_native_preview(trailmate_tdeck ARDUINO_T_DECK tdeck)
add_native_preview(trailmate_wio ARDUINO_WIO_TRACKER_L2 wio-tracker-l2)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_target(native_asset_manifest ALL
  COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/write_manifest.py" "${REPO_ROOT}/site/assets/native"
  DEPENDS trailmate_pager trailmate_tdeck trailmate_wio
  VERBATIM)
