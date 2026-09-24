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
    # app facade
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_app_services.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_app_facade.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/linux_demo_world.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_noop_mesh_adapter.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_raw_lora_mesh_adapter.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/chat/linux_sqlite_chat_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/demo_app.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/app/demo_app_runner.cpp"
    # core primitives
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/core/bitmap_font.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/core/canvas.cpp"
    # platform/linux shared infrastructure (P4)
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/runtime_paths.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/env_config.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/map_contour_tile_generator.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/map_diagnostics.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/map_tile_cache.cpp"
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/map_tile_geometry.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/mesh/linux_mesh_runtime_shell.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/mesh/linux_sqlite_mesh_identity_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/runtime_packet_log.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/linux/sx126x_radio.cpp"
    # platform::ui::* runtime implementations
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/device_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/firmware_update_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/gps_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/map_diagnostics.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/hostlink_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/lora_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/orientation_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/pack_repository_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/route_storage.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/screen_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/reticulum_directory_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/reticulum_group_config_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/settings_store.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/sstv_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/team_ui_store_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/time_runtime.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/platform/ui/tracker_runtime.cpp"
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
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/channel.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/config.pb.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/device_ui.pb.cpp"
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
    "${TRAIL_MATE_REPO_ROOT}/modules/core_hostlink/src/hostlink_session.cpp"
    # modules/core_sys
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/app/app_facade_access.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/reticulum_call_runtime.cpp"
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
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/localization.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/shared_ui_shell.cpp"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}/ui/shell_ui_runner.cpp"
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
    "${TRAIL_MATE_UI_MAP_RUNTIME_SRC_ROOT}/map_tiles/map_tile_resolver.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/team_presentation/team_rich_payload_projector.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/fonts/font_utils.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/info_card.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_nav.cpp"
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
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/extensions/extensions_page_shell.cpp"
    # gnss
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gnss/gnss_skyplot_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gnss/gnss_skyplot_page_shell.cpp"
    # gps
    "${TRAIL_MATE_UI_GPS_RUNTIME_SRC_ROOT}/gps_page_runtime_pump.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gps/gps_page_runtime.cpp"
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
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/top_bar_power_presenter.cpp"
)

# ---------------------------------------------------------------------------
# Common include directories �?reused by every Linux target.
# ---------------------------------------------------------------------------

set(TRAIL_MATE_LINUX_COMMON_INCLUDES
    "${TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT}"
    "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_GENERATED_ROOT}"
    "${TRAIL_MATE_CORE_CHAT_NANOPB_ROOT}"
    "${TRAIL_MATE_CORE_GPS_INCLUDE_ROOT}"
    "${TRAIL_MATE_CORE_HOSTLINK_INCLUDE_ROOT}"
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
    find_package(CURL REQUIRED)
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
    target_compile_definitions(${target_name}
        PUBLIC TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22
    )
    target_link_libraries(${target_name}
        PUBLIC
            Threads::Threads
            CURL::libcurl
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
    set(linux_ui_shell_sources ${TRAIL_MATE_LINUX_UI_SHELL_SOURCES})
    set(linux_ui_shell_includes ${TRAIL_MATE_LINUX_UI_SHELL_INCLUDES})

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
            TRAIL_MATE_CARDPUTER_ZERO_LINUX=1
            GAT562_NO_PINYIN_IME=1
    )
    target_link_libraries(${target_name}
        PUBLIC
            lvgl
            ${common_target}
    )
    trailmate_apply_linux_common_warnings(${target_name})
endfunction()
