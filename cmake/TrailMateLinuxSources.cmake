# TrailMateLinuxSources.cmake
#
# Shared source lists and helper functions for the Trail Mate Linux line.
# removed root linux_sim,
# removed root linux_rpi, and
# removed root linux_uconsole include this file so that a single
# source list drives common app services and core-module targets.
#
# Usage from an app CMakeLists.txt:
#
#   include("${PROJECT_SOURCE_DIR}/../../../cmake/TrailMateLinuxSources.cmake")
#
#   trailmate_add_linux_common(trailmate_cardputer_zero_linux_common)
#   trailmate_add_linux_ui_shell(trailmate_cardputer_zero_ui_shell
#                                 trailmate_cardputer_zero_linux_common)
#
# Then add the app-specific executable (simulator or device) and link against
# these two targets plus any app-specific dependencies.
#
# ---------------------------------------------------------------------------
# Path roots �?derived from this file's location (cmake/), which gives
# the repo root via "..".  Independent of the including app's directory.
# ---------------------------------------------------------------------------

function(_trailmate_set_linux_paths)
    if(NOT DEFINED TRAIL_MATE_REPO_ROOT)
        get_filename_component(TRAIL_MATE_REPO_ROOT
            "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
        set(TRAIL_MATE_REPO_ROOT "${TRAIL_MATE_REPO_ROOT}" PARENT_SCOPE)
    endif()

    set(TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_LINUX_COMMON_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_LINUX_RPI_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/rpi/src"
        PARENT_SCOPE)

    set(TRAIL_MATE_CORE_CHAT_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_CHAT_GENERATED_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_CHAT_NANOPB_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_TEAM_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_GPS_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_HOSTLINK_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_SYS_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_DEVICE_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_device/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_MESH_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CORE_PHONE_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_SHARED_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_SHARED_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_CHAT_RUNTIME_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_CHAT_RUNTIME_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_MAP_RUNTIME_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_GPS_RUNTIME_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_gps_runtime/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_GPS_RUNTIME_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_gps_runtime/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_LVGL_CORE_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_core/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_LVGL_UX_PACKS_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_PRESENTATION_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_UI_PRESENTATION_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_CHAT_PRESENTATION_ADAPTERS_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/include"
        PARENT_SCOPE)
    set(TRAIL_MATE_CHAT_PRESENTATION_ADAPTERS_SRC_ROOT
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src"
        PARENT_SCOPE)
    set(TRAIL_MATE_PLATFORM_SHARED_INCLUDE_ROOT
        "${TRAIL_MATE_REPO_ROOT}/platform/shared/include"
        PARENT_SCOPE)
endfunction()

_trailmate_set_linux_paths()

# ---------------------------------------------------------------------------
# Source lists
# ---------------------------------------------------------------------------

set(TRAIL_MATE_LINUX_COMMON_SOURCES
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/wifi_profile_store.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/audio/audio_focus.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_agenda/src/event_codec.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_agenda/src/recurrence.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_agenda/src/agenda_query.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_agenda/src/agenda_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_agenda/src/reminder_scheduler.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/agenda_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/peer_directory.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/agenda_clock.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/agenda_source.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/agenda/agenda_workspace_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/third_party/yxml/yxml.c"
    # app facade
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_app_services.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_phone_facade.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_phone_bluetooth_config.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_tms_document.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_tms_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_phone_protocol.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_phone_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_app_facade.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_demo_world.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_noop_mesh_adapter.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_raw_lora_mesh_adapter.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_mesh_worker.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshcore_group_wire.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshcore_advert_wire.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshcore_discovery.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshcore_repeater.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_mqtt_client.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshtastic_mqtt_codec.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_mesh_mqtt_bridge.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mqtt/mqtt311_codec.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshcore_direct_wire.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshcore_direct_state.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_meshcore_verification.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/mesh/linux_meshcore_identity.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/mesh/linux_radio_identity_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_sqlite_chat_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/demo_app.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/demo_app_runner.cpp"
    # core primitives
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/core/bitmap_font.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/core/canvas.cpp"
    # platform/linux shared infrastructure (P4)
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/runtime_paths.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/application_lifecycle.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/env_config.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/process_runner.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/iio_sensors.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/network_manager_wifi.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/map_contour_tile_generator.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/map_diagnostics.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/map_tile_cache.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/map_tile_geometry.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/mesh/linux_mesh_runtime_shell.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/mesh/linux_sqlite_mesh_identity_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/runtime_packet_log.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/sx126x_radio.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/native_audio_service.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/backlight.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/gamma_control.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/keyboard_backlight.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/output_power.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/memory_monitor.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/storage_monitor.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/battery_monitor.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/vmp_radio.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/node_identity.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_draft_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/tmap_creation.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_geocaching_drafts.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_crypto.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_record_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_gpx_stage.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_install_journal.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_volume_lease.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_gpx_file_port.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_source.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/geocaching_offline_cache.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/sstv_service.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/walkie_service.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/packet_radio.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/radio_power.cpp"
    # platform::ui::* runtime implementations
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/device_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/firmware_update_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/gps_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/map_diagnostics.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/hostlink_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/lora_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/lora_receive_session.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/agenda_widget_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/music_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/quick_controls_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/resource_lock.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/orientation_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/pack_repository_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/resource_pack_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/rtc_clock.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/network_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/http_client_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/route_image_cache.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_management.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_track_catalog.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_track_snapshots.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_track_prepare.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_track_directories.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/track_directory_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_track_directories.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_track_publication.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/track_publication_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_track_publication.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_track_confirmation.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/track_read_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_track_read.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_identity_backup.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_radio_identity.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/identity_restore_bridge.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/identity_owner_lease.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_config_snapshot.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/tms_snapshot_bridge.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/tms_import_bridge.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_config_transfer.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_uploads.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_package_input.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/stored_route_package.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/hostlink_package_install.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/route_package_images.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/pack_catalog_json.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/remote_pack_catalog.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/remote_pack_archive.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/resource_pack_policy.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/build_identity.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/route_storage.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/screen_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/reticulum_directory_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/reticulum_group_config_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/settings_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/sstv_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/team_ui_store_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/time_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/tracker_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/navigation_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/map_search_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/packaged_map.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/call_audio_service.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/tmap_map_tile_source.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/map_tile_resolver.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/usb_support_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/walkie_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/wireless_companion_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/wifi_runtime.cpp"
    # modules/core_chat
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_action_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_port.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/rnode/rnode_packet_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/mc_region_presets.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_identity_crypto.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_payload_helpers.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/fe.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/ge.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/key_exchange.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/keypair.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sc.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sha512.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sign.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/verify.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/compression/unishox2.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_codec_pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_node_payload.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_aes_ctr.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_pki_crypto.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_radio_config.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_region.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/reticulum_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/announce_verifier.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/path_request_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_packet_router.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_destination_registry.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_link_manager.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/link_handshake_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/interface_access_codec.cpp"
    "${TRAIL_MATE_REPO_ROOT}/third_party/rweather_crypto/SHA256.cpp"
    "${TRAIL_MATE_REPO_ROOT}/third_party/rweather_crypto/Hash.cpp"
    "${TRAIL_MATE_REPO_ROOT}/third_party/rweather_crypto/Crypto.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_link_runtime.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_resource_runtime.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_resource_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_propagation_runtime.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_propagation_service_runtime.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_propagation_sync.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_delivery_planner.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_announce_projection.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_delivery_runtime.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_delivery_attempt_ledger.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_incoming_envelope.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_peer_directory.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_path_manager.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_transport_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/mesh/linux_reticulum_identity.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_ingress.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_links.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_calls.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_tcp.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_network.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_propagation.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_auto.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_network_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/reticulum_network_config_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/settings_reset_runtime.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/app/tms_config_codec.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_announce.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_resources.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_requests.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_outgoing.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_stamp.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_reticulum_upload.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_contact_secrets.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_control_auth.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_control_ingress.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_rs_fec.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_private_crypto.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_media_frames.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_mqtt_transport.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_receive_block.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_session_state_machine.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_voice_inbox.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
    # Meshtastic nanopb runtime + generated descriptors used by Linux LoRa RX/TX.
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/admin.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/channel.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/config.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/connection_status.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/device_ui.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/localonly.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/mesh.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/module_config.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/mqtt.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/portnums.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/telemetry.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/xmodem.pb.cpp"
    # modules/core_team
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_mgmt.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_pairing_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_position.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_track.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_waypoint.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/usecase/team_controller.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/usecase/team_pairing_coordinator.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/usecase/team_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/usecase/team_track_sampler.cpp"
    # modules/core_gps
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_parser.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_sentence.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/gnss_skyplot_presenter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/gps_jitter_filter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/gps_runtime_config.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/gps_runtime_policy.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/gps_runtime_state.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/location_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/time_authority.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/motion_policy.cpp"
    # modules/core_hostlink
    "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/hostlink_codec.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/hostlink_config_codec.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/hostlink_frame_router.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/hostlink_service_codec.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/hostlink_session.cpp"
    # modules/core_sys
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/app/app_facade_access.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/reticulum_call_runtime.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/audio_call_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/lxst_telephony_wire.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/lxst_call_state_machine.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/screen_power_state_machine.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/timezone_profile.cpp"
    # modules/core_device
    "${TRAIL_MATE_REPO_ROOT}/modules/core_device/src/device/capability_types.cpp"
    # modules/core_mesh
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/mc_identity_flow.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/meshcore_protocol_strategy.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshtastic/mt_pki_flow.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshtastic/meshtastic_protocol_strategy.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/direct_message_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_dedup_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_session.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/peer_identity_service.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/receive_packet_service.cpp"
    # modules/core_phone
    "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshcore/meshcore_phone_core.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_core.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_session.cpp"
    # modules/ui_presentation
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/device/device_status_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/gps/gps_status_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/mesh/mesh_status_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/settings/settings_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/chat/chat_workspace_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/key_verification/key_verification_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/map/map_workspace_model.cpp"
    # modules/chat_presentation_adapters
    "${TRAIL_MATE_CHAT_PRESENTATION_ADAPTERS_SRC_ROOT}/chat_conversation_mapper.cpp"
    "${TRAIL_MATE_CHAT_PRESENTATION_ADAPTERS_SRC_ROOT}/chat_message_mapper.cpp"
    # shared Team presentation projections consumed by Linux runtime models
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/team_map_overlay_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/team_presence/team_presence_model.cpp"
)

set(TRAIL_MATE_LINUX_UI_SHELL_SOURCES
    # platform/linux/common �?shared Linux UI layer
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/shared_ui_shell.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/shell_ui_runner.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/voice_runtime_binding.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/phone_pairing_binding.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/agenda_runtime_binding.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/agenda.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_state.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_editor_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_editor_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_page_flow.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/agenda/agenda_reminder_popup.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/ui_common.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/ui_status.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/widgets/map/map_tiles.cpp"
    # modules/ui_shared �?assets
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Chat.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/alert.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/AreaCleared.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/BaseCamp.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/contact.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/ext.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/fsk_mod_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/gps.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/gps_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/GoodFind.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/img_usb.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/logo.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/lora_mod_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/message_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/nomad.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/rally.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/rf.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/room-24px.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/route_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Satellite.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Setting.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/radar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/sstv.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/sos.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/shutdown.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/team.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/team_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/tracker.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/tracker_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/walkie_talkie.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/walkie_monitor_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/wifi_topbar.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/ble_topbar.c"
    # modules/ui_shared �?components
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/air_status_footer.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/floating_search_box.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/map_poi_search.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/screen_saver_overlay.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/shortcut_help_modal.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/runtime_device_status_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/runtime_mesh_status_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/runtime_settings_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/runtime_gps_status_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/runtime_map_workspace_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/runtime_chat_action_sink.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/chat_presentation_source.cpp"
    "${TRAIL_MATE_UI_CHAT_RUNTIME_SRC_ROOT}/chat_delivery_action_port_adapter.cpp"
    "${TRAIL_MATE_UI_CHAT_RUNTIME_SRC_ROOT}/chat_delivery_feedback_controller.cpp"
    "${TRAIL_MATE_UI_CHAT_RUNTIME_SRC_ROOT}/chat_delivery_event_projection_adapter.cpp"
    "${TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_SRC_ROOT}/key_verification_action_sink.cpp"
    "${TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_SRC_ROOT}/key_verification_presentation_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/team_chat_presentation_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/presentation_sources/team_chat_action_sink.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/team_actions/team_action_runtime_sink.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/team_actions/team_runtime_adapters.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/team_persistence/team_ui_snapshot_codec.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_overlay_projection_adapter.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_overlay_snapshot_source.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_overlay/map_overlay_projector.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/filesystem_map_tile_source.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/map_tile_async_runtime.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/map_tile_render_queue.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_poi/annotation_frame.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_poi/annotation_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/map/poi_overlay.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/team_presentation/team_rich_payload_projector.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/fonts/font_utils.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/info_card.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_nav.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/bottom_bar.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/dialog_frame.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_styles.cpp"
    # modules/ui_shared �?shell / menu / page
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/app_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/chat_voice_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/app_catalog_builder.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/formatters.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/loop_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/startup_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/watch_face.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_compass_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_agenda_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_music_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_controls_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/music.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_gps_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_mesh_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_recent_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_state.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_dashboard.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_style.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/page/page_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/common/placeholder_page.cpp"
    # chat
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_styles.cpp"
    "${TRAIL_MATE_UI_CHAT_RUNTIME_SRC_ROOT}/chat_page_runtime_event_pump.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_protocol_support.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_team_workflow.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_ui_controller.cpp"
    "${TRAIL_MATE_UI_PRESENTATION_SRC_ROOT}/menu/menu_model.cpp"
    "${TRAIL_MATE_UI_PRESENTATION_SRC_ROOT}/page/page_manifest.cpp"
    "${TRAIL_MATE_UI_PRESENTATION_SRC_ROOT}/screen/screen_binding_registry.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/ux/screen_registry.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/ux/input_binding_set.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/ux/ux_menu_model.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/ux/ux_screen_menu_adapter.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/ux/ux_menu_provider.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/ux/ux_pack_registry.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/runtime/compatibility_screen_factory.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/runtime/lvgl_menu_runtime_adapter.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/runtime/lvgl_screen_host_adapter.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/runtime/lvgl_screen_graph_bridge.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/packs/cardputer_compact_ux_pack.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/packs/compatibility_ux_pack.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/packs/deck_touch_ux_pack.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/packs/uconsole_desktop_ux_pack.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/packs/tiny_node_status_ux_pack.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/packs/t_display_p4_touch_ux_pack.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/packs/simulator_full_ux_pack.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/common/key_verification_modal_renderer.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/common/input_layout.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/common/touch_text_editor.cpp"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_SRC_ROOT}/common/team_position_picker_renderer.cpp"
    # contacts
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_team_snapshot_source.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_state.cpp"
    # energy sweep
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/energy_sweep/energy_sweep_page_shell.cpp"
    # extensions
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/extensions/extensions_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/extensions/tmap_creator_page.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/extensions/extensions_page_shell.cpp"
    # gnss
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gnss/gnss_skyplot_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gnss/gnss_skyplot_page_shell.cpp"
    # gps
    "${TRAIL_MATE_UI_GPS_RUNTIME_SRC_ROOT}/gps_page_runtime_pump.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gps/gps_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/geocaching/geocaching_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gps/gps_page_shell.cpp"
    # node info
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/node_info/node_info_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/node_info/node_info_page_layout.cpp"
    # network
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/network/micron_markup_contract.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/network/network_page_shell.cpp"
    # settings
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_channel_actions.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_spec.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_state.cpp"
    # sstv
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/sstv/sstv_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/sstv/sstv_page_shell.cpp"
    # team
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_activity_adapters.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_activity_sink.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_command_reducer.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_create_team_action.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_deferred_dispatch.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_deferred_dispatch_adapters.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_event_effect_sink.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_event_reducer.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_flow_controller.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_key_request_action.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_key_event_log.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_kick_confirm_action.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_lvgl_renderer.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_pairing_command_action.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_read_model.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_request_keys_action.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_runtime_adapters.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_runtime_port.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_state_store.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_transfer_leader_action.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_styles.cpp"
    # tracker
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_input.cpp"
      "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_navigation_dialog.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_state.cpp"
    # usb
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/usb/usb_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/usb/usb_page_shell.cpp"
    # walkie talkie
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/walkie_talkie/walkie_talkie_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/walkie_talkie/walkie_talkie_page_shell.cpp"
    # startup / widgets
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/startup_ui_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/ui_boot.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/runtime/memory_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/runtime/ui_feedback.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/i18n/resource_pack_registry.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/ime/ime_input_mode_descriptor.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/ime/ime_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/busy_overlay.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/foreground_operation_overlay.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/map/map_viewport.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/progress_overlay_presenter.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/reticulum_call_overlay.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/reticulum_ping_overlay.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/route_elevation_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/route_image_operation_presenter.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/route_image_strip.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/system_notification.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/text_candidate_data.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/text_candidate_picker.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/toast/toast_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/top_bar.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/input/pager_navigation.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/ble_pairing_popup.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/top_bar_power_presenter.cpp"
)

# ---------------------------------------------------------------------------
# Common include directories �?reused by every Linux target.
# ---------------------------------------------------------------------------

set(TRAIL_MATE_LINUX_COMMON_INCLUDES
    "${TRAIL_MATE_REPO_ROOT}/third_party/rweather_crypto"
    "${TRAIL_MATE_PLATFORM_SHARED_INCLUDE_ROOT}"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_agenda/include"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_music/include"
    "${TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT}"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_GENERATED_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_NANOPB_ROOT}"
    "${TRAIL_MATE_CORE_GPS_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_HOSTLINK_INCLUDE_ROOT}"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_track_sharing/include"
    "${TRAIL_MATE_REPO_ROOT}/third_party/yxml"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_geocaching/include"
    "${TRAIL_MATE_CORE_SYS_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_DEVICE_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_MESH_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_PHONE_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_TEAM_INCLUDE_ROOT}"
    "${TRAIL_MATE_CHAT_PRESENTATION_ADAPTERS_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_PRESENTATION_INCLUDE_ROOT}"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include"
    "${TRAIL_MATE_UI_SHARED_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_CHAT_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_MAP_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_GPS_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_LVGL_CORE_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_INCLUDE_ROOT}"
)

set(TRAIL_MATE_LINUX_UI_SHELL_INCLUDES
    "${TRAIL_MATE_REPO_ROOT}/modules/core_agenda/include"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_music/include"
    "${TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT}"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_GENERATED_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_NANOPB_ROOT}"
    "${TRAIL_MATE_CORE_TEAM_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_SHARED_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_CHAT_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_KEY_VERIFICATION_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_MAP_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_GPS_RUNTIME_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_LVGL_CORE_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_LVGL_UX_PACKS_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_SYS_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_DEVICE_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_PHONE_INCLUDE_ROOT}"
    "${TRAIL_MATE_CHAT_PRESENTATION_ADAPTERS_INCLUDE_ROOT}"
    "${TRAIL_MATE_UI_PRESENTATION_INCLUDE_ROOT}"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include"
    "${TRAIL_MATE_PLATFORM_SHARED_INCLUDE_ROOT}"
)

# ---------------------------------------------------------------------------
# Helper functions
# ---------------------------------------------------------------------------

function(trailmate_apply_linux_common_warnings target_name)
    if(MSVC)
        target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /FS)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

function(trailmate_add_linux_common target_name)
    include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateProjectVersion.cmake")
    if(NOT TARGET trailmate_linux_bzip2)
        set(bzip2_root "${TRAIL_MATE_REPO_ROOT}/third_party/bzip2/src")
        add_library(trailmate_linux_bzip2 STATIC
            "${bzip2_root}/blocksort.c" "${bzip2_root}/compress.c"
            "${bzip2_root}/decompress.c" "${bzip2_root}/bzlib.c"
            "${bzip2_root}/crctable.c" "${bzip2_root}/randtable.c"
            "${bzip2_root}/huffman.c" "${bzip2_root}/bz_internal_error.c")
        target_compile_definitions(trailmate_linux_bzip2 PRIVATE BZ_NO_STDIO)
        target_include_directories(trailmate_linux_bzip2 PUBLIC "${bzip2_root}")
    endif()
    include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxGnss.cmake")
    trailmate_add_linux_gnss()
    include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxBluez.cmake")
    trailmate_add_linux_bluez()
    cmake_parse_arguments(LINUX_COMMON "ENABLE_LR2021;ENABLE_MEDIA" "" "" ${ARGN})
    include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateTmap.cmake")
    trailmate_add_tmap()
    find_package(CURL REQUIRED)
    find_package(JPEG REQUIRED)
    find_package(PNG REQUIRED)
    find_package(LibArchive 3.3 REQUIRED)
    find_package(SQLite3 REQUIRED)
    find_package(OpenSSL QUIET)

    set(linux_common_sources ${TRAIL_MATE_LINUX_COMMON_SOURCES})
    set(linux_common_includes ${TRAIL_MATE_LINUX_COMMON_INCLUDES})

    add_library(${target_name}
        ${linux_common_sources}
    )
    target_include_directories(${target_name}
        PUBLIC ${linux_common_includes}
    )
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    set_property(SOURCE "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/build_identity.cpp"
        APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_BINARY_DIR}/generated")
    if(LINUX_COMMON_ENABLE_MEDIA)
        include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxMedia.cmake")
        trailmate_enable_linux_media(${target_name})
    endif()
    if(LINUX_COMMON_ENABLE_LR2021 AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
        include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxRadioLib.cmake")
        trailmate_add_linux_lr2021(trailmate_linux_lr2021)
        target_compile_definitions(${target_name} PRIVATE TRAIL_MATE_LINUX_HAS_LR2021=1)
        target_link_libraries(${target_name} PRIVATE trailmate_linux_lr2021)
    endif()
    target_compile_definitions(${target_name}
        PUBLIC TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22
    )
    target_link_libraries(${target_name}
        PUBLIC
            tmap::tmap
            trailmate_linux_gnss_source
            trailmate_linux_bzip2
            trailmate_linux_bluez
            Threads::Threads
            CURL::libcurl
            JPEG::JPEG
            PNG::PNG
            LibArchive::LibArchive
            SQLite::SQLite3
    )
    if(OpenSSL_FOUND)
        target_compile_definitions(${target_name}
            PUBLIC TRAIL_MATE_HAS_OPENSSL=1
        )
        target_link_libraries(${target_name}
            PUBLIC OpenSSL::Crypto
        )
    endif()
    if(WIN32)
        target_link_libraries(${target_name} PUBLIC ws2_32)
    endif()
    trailmate_apply_linux_common_warnings(${target_name})
endfunction()

function(trailmate_add_linux_ui_shell target_name common_target)
    if(NOT TARGET trailmate_linux_waypoint)
        add_library(trailmate_linux_waypoint
            "${TRAIL_MATE_REPO_ROOT}/modules/core_waypoint/src/waypoint.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_waypoint/src/record_codec.cpp"
            "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/waypoint_store.cpp"
            "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/waypoint_source.cpp")
        target_include_directories(trailmate_linux_waypoint PUBLIC
            "${TRAIL_MATE_REPO_ROOT}/modules/core_waypoint/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT}")
        target_link_libraries(trailmate_linux_waypoint PUBLIC SQLite::SQLite3 Threads::Threads)
        target_compile_features(trailmate_linux_waypoint PUBLIC cxx_std_17)
    endif()
    cmake_parse_arguments(UI "" "UX_PACK" "" ${ARGN})
    if(NOT UI_UX_PACK)
        set(UI_UX_PACK "cardputer_compact")
    endif()
    set(linux_ui_shell_sources ${TRAIL_MATE_LINUX_UI_SHELL_SOURCES})
    set(linux_ui_shell_includes ${TRAIL_MATE_LINUX_UI_SHELL_INCLUDES})
    if(NOT UI_UX_PACK STREQUAL "cardputer_compact")
        # Legacy compact compositions own these startup/session wrappers.
        # Configurable desktop compositions provide their own catalog hooks.
        list(REMOVE_ITEM linux_ui_shell_sources
            "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/shared_ui_shell.cpp"
            "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/shell_ui_runner.cpp")
    endif()

    # common_target must already be created (via trailmate_add_linux_common).
    add_library(${target_name}
        ${linux_ui_shell_sources}
    )
    target_include_directories(${target_name}
        PUBLIC ${linux_ui_shell_includes}
    )
    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_compile_definitions(
        ${target_name}
        PRIVATE
            TRAIL_MATE_BOOT_MIN_SHOW_MS=1200
            GAT562_NO_PINYIN_IME=1
            TRAIL_MATE_PLATFORM_LINUX=1
    )
    if(UI_UX_PACK STREQUAL "cardputer_compact")
        target_compile_definitions(${target_name} PRIVATE TRAIL_MATE_CARDPUTER_ZERO_LINUX=1)
    elseif(UI_UX_PACK STREQUAL "t_display_p4_touch")
        target_compile_definitions(${target_name} PRIVATE TRAIL_MATE_UI_P4_TOUCH=1)
        target_sources(${target_name} PRIVATE
            "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/music/music_page_layout.cpp"
            "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/music/music_page_input.cpp"
            "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/music/music_state.cpp"
            "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/music/music_page_shell.cpp")
    else()
        message(FATAL_ERROR "Unsupported Linux LVGL UX pack: ${UI_UX_PACK}")
    endif()
    target_sources(${target_name} PRIVATE
        "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/calc.c"
        "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/geocaching.c"
        "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/calculator/calculator_engine.cpp"
        "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/calculator/calculator_page_layout.cpp"
        "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/calculator/calculator_page_runtime.cpp"
        "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/calculator/calculator_page_shell.cpp")
    target_link_libraries(${target_name}
        PUBLIC
            lvgl
            ${common_target}
            trailmate_linux_waypoint
    )
    trailmate_apply_linux_common_warnings(${target_name})
endfunction()
