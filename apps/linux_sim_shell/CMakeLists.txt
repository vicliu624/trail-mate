cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxSimShell LANGUAGES CXX)

include(CTest)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

set(TRAIL_MATE_MESHTASTIC_GENERATED_SOURCES
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
    "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/xmodem.pb.cpp")

add_library(trailmate_linux_sim_shell
    src/linux_sim_app_shell.cpp
    src/linux_sim_runtime_entry.cpp
    src/linux_sim_runtime_entry_adoption_probe.cpp
    src/linux_sim_runtime_renderer.cpp
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_ux_binding.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_menu_runtime_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_screen_host_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_screen_graph_bridge.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_runtime_screen_graph_presenter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_runtime_entry_adoption.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_descriptor_renderer.cpp")

target_include_directories(trailmate_linux_sim_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/include")

target_link_libraries(trailmate_linux_sim_shell
    PUBLIC
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_linux_sim_shell PUBLIC cxx_std_17)

if(BUILD_TESTING)
    enable_language(C)
    find_package(SQLite3 QUIET)
    find_package(OpenSSL QUIET)

    add_executable(trailmate_calculator_engine_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_calculator_engine.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/calculator/calculator_engine.cpp")
    target_include_directories(trailmate_calculator_engine_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_features(trailmate_calculator_engine_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_calculator_engine_smoke
             COMMAND trailmate_calculator_engine_smoke)

    add_executable(trailmate_linux_sim_app_shell_smoke
        tests/linux_sim_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_app_shell_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_app_shell_smoke
             COMMAND trailmate_linux_sim_app_shell_smoke)

    add_executable(trailmate_linux_sim_runtime_entry_adoption_probe_smoke
        tests/linux_sim_runtime_entry_adoption_probe_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_entry_adoption_probe_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_entry_adoption_probe_smoke
             COMMAND trailmate_linux_sim_runtime_entry_adoption_probe_smoke)

    add_executable(trailmate_linux_sim_runtime_entry_smoke
        tests/linux_sim_runtime_entry_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_entry_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_entry_smoke
             COMMAND trailmate_linux_sim_runtime_entry_smoke)

    add_executable(trailmate_linux_sim_runtime_renderer_smoke
        tests/linux_sim_runtime_renderer_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_renderer_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_renderer_smoke
             COMMAND trailmate_linux_sim_runtime_renderer_smoke)

    add_executable(trailmate_ascii_runtime_entry_adoption_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/tests/ascii_runtime_entry_adoption_smoke.cpp")
    target_link_libraries(trailmate_ascii_runtime_entry_adoption_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_ascii_runtime_entry_adoption_smoke
             COMMAND trailmate_ascii_runtime_entry_adoption_smoke)

    add_executable(trailmate_ascii_descriptor_renderer_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/tests/test_ascii_descriptor_renderer.cpp")
    target_link_libraries(trailmate_ascii_descriptor_renderer_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_ascii_descriptor_renderer_smoke
             COMMAND trailmate_ascii_descriptor_renderer_smoke)

    add_executable(trailmate_chat_presentation_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_conversation_mapper.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_message_mapper.cpp")
    target_include_directories(trailmate_chat_presentation_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/include")
    target_compile_features(trailmate_chat_presentation_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_presentation_source_smoke
             COMMAND trailmate_chat_presentation_source_smoke)

    add_executable(trailmate_chat_conversation_mapper_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/tests/test_chat_conversation_mapper.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_conversation_mapper.cpp")
    target_include_directories(trailmate_chat_conversation_mapper_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_chat_conversation_mapper_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_conversation_mapper_smoke
             COMMAND trailmate_chat_conversation_mapper_smoke)

    add_executable(trailmate_mesh_protocol_utils_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_mesh_protocol_utils.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp")
    target_include_directories(trailmate_mesh_protocol_utils_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_mesh_protocol_utils_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_mesh_protocol_utils_smoke
             COMMAND trailmate_mesh_protocol_utils_smoke)

    if(SQLite3_FOUND)
        add_executable(trailmate_uconsole_chat_sqlite_store_smoke
            "${TRAIL_MATE_REPO_ROOT}/apps/linux_uconsole_gtk/tests/uconsole_chat_sqlite_store_smoke.cpp"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/chat/linux_sqlite_chat_store.cpp"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
        target_include_directories(trailmate_uconsole_chat_sqlite_store_smoke
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
        target_compile_features(trailmate_uconsole_chat_sqlite_store_smoke
            PRIVATE cxx_std_17)
        target_link_libraries(trailmate_uconsole_chat_sqlite_store_smoke
            PRIVATE SQLite::SQLite3)
        add_test(NAME trailmate_uconsole_chat_sqlite_store_smoke
                 COMMAND trailmate_uconsole_chat_sqlite_store_smoke)
    endif()

    add_executable(trailmate_chat_service_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_chat_service_resend.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_chat_service_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_chat_service_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_service_smoke
             COMMAND trailmate_chat_service_smoke)

    add_executable(trailmate_chat_delivery_message_projection_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp")
    target_include_directories(trailmate_chat_delivery_message_projection_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_chat_delivery_message_projection_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_delivery_message_projection_smoke
             COMMAND trailmate_chat_delivery_message_projection_smoke)

    add_executable(trailmate_chat_delivery_send_result_projection_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_chat_delivery_send_result_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp")
    target_include_directories(trailmate_chat_delivery_send_result_projection_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_chat_delivery_send_result_projection_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_delivery_send_result_projection_smoke
             COMMAND trailmate_chat_delivery_send_result_projection_smoke)

    add_executable(trailmate_chat_outbox_service_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_chat_outbox_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp")
    target_include_directories(trailmate_chat_outbox_service_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_chat_outbox_service_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_outbox_service_smoke
             COMMAND trailmate_chat_outbox_service_smoke)

    add_executable(trailmate_chat_message_ledger_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_chat_message_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp")
    target_include_directories(trailmate_chat_message_ledger_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_chat_message_ledger_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_message_ledger_smoke
             COMMAND trailmate_chat_message_ledger_smoke)

    add_executable(trailmate_chat_read_state_ledger_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_chat_read_state_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp")
    target_include_directories(trailmate_chat_read_state_ledger_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_chat_read_state_ledger_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_read_state_ledger_smoke
             COMMAND trailmate_chat_read_state_ledger_smoke)

    add_executable(trailmate_esp_sd_store_read_state_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_esp_sd_store_read_state_contract.cpp")
    target_compile_features(trailmate_esp_sd_store_read_state_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_esp_sd_store_read_state_contract_smoke
             COMMAND trailmate_esp_sd_store_read_state_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_reticulum_projection_policy_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_reticulum_projection_policy.cpp")
    target_include_directories(trailmate_reticulum_projection_policy_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_reticulum_projection_policy_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_reticulum_projection_policy_smoke
             COMMAND trailmate_reticulum_projection_policy_smoke)

    add_executable(trailmate_reticulum_call_product_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_reticulum_call_product_contract.cpp")
    target_compile_features(trailmate_reticulum_call_product_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_reticulum_call_product_contract_smoke
             COMMAND trailmate_reticulum_call_product_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_vmp_wire_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp")
    target_include_directories(trailmate_vmp_wire_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_wire_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_wire_smoke
             COMMAND trailmate_vmp_wire_smoke)

    add_executable(trailmate_vmp_contact_secrets_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_contact_secrets.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_private_crypto.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_contact_secrets.cpp")
    target_include_directories(trailmate_vmp_contact_secrets_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_contact_secrets_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_contact_secrets_smoke
             COMMAND trailmate_vmp_contact_secrets_smoke)

    add_executable(trailmate_vmp_control_ingress_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_control_ingress.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_control_ingress.cpp")
    target_include_directories(trailmate_vmp_control_ingress_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_control_ingress_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_control_ingress_smoke
             COMMAND trailmate_vmp_control_ingress_smoke)

    add_executable(trailmate_vmp_session_state_machine_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_session_state_machine.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_session_state_machine.cpp")
    target_include_directories(trailmate_vmp_session_state_machine_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_session_state_machine_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_session_state_machine_smoke
             COMMAND trailmate_vmp_session_state_machine_smoke)

    add_executable(trailmate_vmp_rs_fec_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_rs_fec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_rs_fec.cpp")
    target_include_directories(trailmate_vmp_rs_fec_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_rs_fec_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_rs_fec_smoke
             COMMAND trailmate_vmp_rs_fec_smoke)

    add_executable(trailmate_vmp_receive_block_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_receive_block.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_rs_fec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_receive_block.cpp")
    target_include_directories(trailmate_vmp_receive_block_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_receive_block_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_receive_block_smoke
             COMMAND trailmate_vmp_receive_block_smoke)

    add_executable(trailmate_vmp_media_frames_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_media_frames.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_rs_fec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_private_crypto.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_media_frames.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_receive_block.cpp")
    target_include_directories(trailmate_vmp_media_frames_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_media_frames_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_media_frames_smoke
             COMMAND trailmate_vmp_media_frames_smoke)

    add_executable(trailmate_vmp_voice_inbox_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_voice_inbox.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_voice_inbox.cpp")
    target_include_directories(trailmate_vmp_voice_inbox_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_voice_inbox_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_voice_inbox_smoke
             COMMAND trailmate_vmp_voice_inbox_smoke)

    add_executable(trailmate_vmp_attachment_persistence_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_attachment_persistence_contract.cpp")
    target_compile_features(trailmate_vmp_attachment_persistence_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_attachment_persistence_contract_smoke
             COMMAND trailmate_vmp_attachment_persistence_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_chat_voice_runtime_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_chat_voice_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/chat_voice_runtime.cpp")
    target_include_directories(trailmate_chat_voice_runtime_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_features(trailmate_chat_voice_runtime_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_voice_runtime_smoke
             COMMAND trailmate_chat_voice_runtime_smoke)

    add_executable(trailmate_vmp_mqtt_isolation_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_mqtt_isolation_contract.cpp")
    target_compile_features(trailmate_vmp_mqtt_isolation_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_mqtt_isolation_contract_smoke
             COMMAND trailmate_vmp_mqtt_isolation_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_vmp_lxmf_isolation_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_lxmf_isolation_contract.cpp")
    target_compile_features(trailmate_vmp_lxmf_isolation_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_lxmf_isolation_contract_smoke
             COMMAND trailmate_vmp_lxmf_isolation_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_vmp_control_auth_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_control_auth.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_private_crypto.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_control_auth.cpp")
    target_include_directories(trailmate_vmp_control_auth_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_vmp_control_auth_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_vmp_control_auth_smoke
             COMMAND trailmate_vmp_control_auth_smoke)

    if(OpenSSL_FOUND)
        add_executable(trailmate_vmp_private_crypto_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_private_crypto.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_private_crypto.cpp")
        target_include_directories(trailmate_vmp_private_crypto_smoke
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
        target_compile_definitions(trailmate_vmp_private_crypto_smoke
            PRIVATE TRAIL_MATE_HAS_OPENSSL=1)
        target_link_libraries(trailmate_vmp_private_crypto_smoke
            PRIVATE OpenSSL::Crypto)
        target_compile_features(trailmate_vmp_private_crypto_smoke
            PRIVATE cxx_std_17)
        add_test(NAME trailmate_vmp_private_crypto_smoke
                 COMMAND trailmate_vmp_private_crypto_smoke)

        add_executable(trailmate_vmp_mqtt_transport_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_vmp_mqtt_transport.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_wire.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_contact_secrets.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_control_auth.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_private_crypto.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_rs_fec.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_media_frames.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_receive_block.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/voice/vmp_mqtt_transport.cpp")
        target_include_directories(trailmate_vmp_mqtt_transport_smoke
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
        target_compile_definitions(trailmate_vmp_mqtt_transport_smoke
            PRIVATE TRAIL_MATE_HAS_OPENSSL=1)
        target_link_libraries(trailmate_vmp_mqtt_transport_smoke
            PRIVATE OpenSSL::Crypto)
        target_compile_features(trailmate_vmp_mqtt_transport_smoke
            PRIVATE cxx_std_17)
        add_test(NAME trailmate_vmp_mqtt_transport_smoke
                 COMMAND trailmate_vmp_mqtt_transport_smoke)
    endif()

    add_executable(trailmate_lxmf_runtime_budget_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_lxmf_runtime_budget.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_runtime_budget.cpp")
    target_include_directories(trailmate_lxmf_runtime_budget_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/include")
    target_compile_features(trailmate_lxmf_runtime_budget_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_lxmf_runtime_budget_smoke
             COMMAND trailmate_lxmf_runtime_budget_smoke)

    add_executable(trailmate_lxmf_announce_scheduler_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_lxmf_announce_scheduler.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_announce_scheduler.cpp")
    target_include_directories(trailmate_lxmf_announce_scheduler_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/include")
    target_compile_features(trailmate_lxmf_announce_scheduler_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_lxmf_announce_scheduler_smoke
             COMMAND trailmate_lxmf_announce_scheduler_smoke)

    add_executable(trailmate_lxmf_deferred_discovery_queue_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_lxmf_deferred_discovery_queue.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_deferred_discovery_queue.cpp")
    target_include_directories(trailmate_lxmf_deferred_discovery_queue_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/shared/include")
    target_compile_features(trailmate_lxmf_deferred_discovery_queue_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_lxmf_deferred_discovery_queue_smoke
             COMMAND trailmate_lxmf_deferred_discovery_queue_smoke)

    add_executable(trailmate_storage_maintenance_state_machine_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/common/tests/test_storage_maintenance_state_machine.cpp")
    target_include_directories(trailmate_storage_maintenance_state_machine_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/platform/esp/common/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_storage_maintenance_state_machine_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_storage_maintenance_state_machine_smoke
             COMMAND trailmate_storage_maintenance_state_machine_smoke)

    add_executable(trailmate_protocol_chat_storage_v2_codec_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_protocol_chat_storage_v2_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/store/protocol_chat_codec.cpp")
    target_include_directories(trailmate_protocol_chat_storage_v2_codec_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/include")
    target_compile_features(trailmate_protocol_chat_storage_v2_codec_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_protocol_chat_storage_v2_codec_smoke
             COMMAND trailmate_protocol_chat_storage_v2_codec_smoke)

    add_executable(trailmate_protocol_peer_storage_v2_codec_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_protocol_peer_storage_v2_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/store/protocol_peer_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/store/protocol_chat_codec.cpp")
    target_include_directories(trailmate_protocol_peer_storage_v2_codec_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/include")
    target_compile_features(trailmate_protocol_peer_storage_v2_codec_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_protocol_peer_storage_v2_codec_smoke
             COMMAND trailmate_protocol_peer_storage_v2_codec_smoke)

    add_executable(trailmate_chat_delivery_event_projection_adapter_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/tests/test_chat_delivery_event_projection_adapter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_port.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_chat_delivery_event_projection_adapter_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_chat_delivery_event_projection_adapter_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_delivery_event_projection_adapter_smoke
             COMMAND trailmate_chat_delivery_event_projection_adapter_smoke)

    add_executable(trailmate_linux_chat_voice_event_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/tests/test_chat_voice_event.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_linux_chat_voice_event_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_linux_chat_voice_event_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_linux_chat_voice_event_smoke
             COMMAND trailmate_linux_chat_voice_event_smoke)

    add_executable(trailmate_mesh_peer_directory_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_mesh_peer_directory_contract.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_mesh_peer_directory_contract_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_mesh_peer_directory_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_mesh_peer_directory_contract_smoke
             COMMAND trailmate_mesh_peer_directory_contract_smoke)

    add_executable(trailmate_linux_reticulum_directory_runtime_smoke
        tests/linux_reticulum_directory_runtime_smoke.cpp
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/reticulum_directory_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp")
    target_include_directories(trailmate_linux_reticulum_directory_runtime_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_linux_reticulum_directory_runtime_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_linux_reticulum_directory_runtime_smoke
             COMMAND trailmate_linux_reticulum_directory_runtime_smoke)

    add_executable(trailmate_mesh_adapter_router_core_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_mesh_adapter_router_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_adapter_router_core.cpp")
    target_include_directories(trailmate_mesh_adapter_router_core_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_mesh_adapter_router_core_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_mesh_adapter_router_core_smoke
             COMMAND trailmate_mesh_adapter_router_core_smoke)

    add_executable(trailmate_meshtastic_protocol_policy_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_meshtastic_protocol_policy.cpp")
    target_include_directories(trailmate_meshtastic_protocol_policy_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb")
    target_compile_features(trailmate_meshtastic_protocol_policy_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshtastic_protocol_policy_smoke
             COMMAND trailmate_meshtastic_protocol_policy_smoke)

    add_executable(trailmate_auto_reply_policy_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_auto_reply_policy.cpp")
    target_include_directories(trailmate_auto_reply_policy_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_auto_reply_policy_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_auto_reply_policy_smoke
             COMMAND trailmate_auto_reply_policy_smoke)

    add_executable(trailmate_settings_text_font_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_settings_text_font_contract.cpp")
    target_compile_features(trailmate_settings_text_font_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_settings_text_font_contract_smoke
             COMMAND trailmate_settings_text_font_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp")

    add_executable(trailmate_auto_reply_settings_persistence_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_auto_reply_settings_persistence_contract.cpp")
    target_compile_features(trailmate_auto_reply_settings_persistence_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_auto_reply_settings_persistence_contract_smoke
             COMMAND trailmate_auto_reply_settings_persistence_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_reticulum_network_defaults_refresh_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_reticulum_network_defaults_refresh_contract.cpp")
    target_compile_features(trailmate_reticulum_network_defaults_refresh_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_reticulum_network_defaults_refresh_contract_smoke
             COMMAND trailmate_reticulum_network_defaults_refresh_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_reticulum_wifi_gateway_pending_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/tests/test_reticulum_wifi_gateway_pending_contract.cpp")
    target_compile_features(trailmate_reticulum_wifi_gateway_pending_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_reticulum_wifi_gateway_pending_contract_smoke
             COMMAND trailmate_reticulum_wifi_gateway_pending_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_mt_dedup_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_mt_dedup.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_dedup.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_mt_dedup_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_mt_dedup_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_mt_dedup_smoke
             COMMAND trailmate_mt_dedup_smoke)

    add_executable(trailmate_meshtastic_mqtt_downlink_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_meshtastic_mqtt_downlink_contract.cpp")
    target_compile_features(trailmate_meshtastic_mqtt_downlink_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshtastic_mqtt_downlink_contract_smoke
             COMMAND trailmate_meshtastic_mqtt_downlink_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_protocol_effect_executor_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_protocol_effect_executor.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_aes_ctr.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_position_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_waypoint_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c"
        ${TRAIL_MATE_MESHTASTIC_GENERATED_SOURCES})
    target_include_directories(trailmate_protocol_effect_executor_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb")
    target_compile_features(trailmate_protocol_effect_executor_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_protocol_effect_executor_smoke
             COMMAND trailmate_protocol_effect_executor_smoke)

    add_executable(trailmate_meshcore_runtime_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_meshcore_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_payload_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers.cpp")
    target_include_directories(trailmate_meshcore_runtime_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_meshcore_runtime_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshcore_runtime_smoke
             COMMAND trailmate_meshcore_runtime_smoke)

    add_executable(trailmate_mesh_protocol_facade_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_mesh_protocol_facade.cpp")
    target_include_directories(trailmate_mesh_protocol_facade_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_mesh_protocol_facade_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_mesh_protocol_facade_smoke
             COMMAND trailmate_mesh_protocol_facade_smoke)

    add_executable(trailmate_protocol_runtime_factory_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_protocol_runtime_factory.cpp")
    target_include_directories(trailmate_protocol_runtime_factory_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_protocol_runtime_factory_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_protocol_runtime_factory_smoke
             COMMAND trailmate_protocol_runtime_factory_smoke)

    add_executable(trailmate_reticulum_announce_vectors_smoke
        "${TRAIL_MATE_REPO_ROOT}/tests/reticulum_conformance/test_reticulum_announce_vectors.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/reticulum_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/fe.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/ge.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/key_exchange.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/keypair.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sc.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sha512.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sign.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/verify.c")
    target_include_directories(trailmate_reticulum_announce_vectors_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_definitions(trailmate_reticulum_announce_vectors_smoke
        PRIVATE TRAIL_MATE_RETICULUM_PARSE_ONLY=1)
    target_compile_features(trailmate_reticulum_announce_vectors_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_reticulum_announce_vectors_smoke
             COMMAND trailmate_reticulum_announce_vectors_smoke)

    add_executable(trailmate_reticulum_supported_subset_vectors_smoke
        "${TRAIL_MATE_REPO_ROOT}/tests/reticulum_conformance/test_reticulum_supported_subset_vectors.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/lxst_call_state_machine.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/lxst_telephony_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/reticulum_wire.cpp")
    target_include_directories(trailmate_reticulum_supported_subset_vectors_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_definitions(trailmate_reticulum_supported_subset_vectors_smoke
        PRIVATE TRAIL_MATE_RETICULUM_HASH_ONLY=1)
    target_compile_features(trailmate_reticulum_supported_subset_vectors_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_reticulum_supported_subset_vectors_smoke
             COMMAND trailmate_reticulum_supported_subset_vectors_smoke)

    if(OpenSSL_FOUND)
        add_executable(trailmate_reticulum_token_crypto_smoke
            "${TRAIL_MATE_REPO_ROOT}/tests/reticulum_conformance/test_reticulum_token_crypto.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/reticulum_wire.cpp")
        target_include_directories(trailmate_reticulum_token_crypto_smoke
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
        target_compile_definitions(trailmate_reticulum_token_crypto_smoke
            PRIVATE
                TRAIL_MATE_HAS_OPENSSL=1
                TRAIL_MATE_RETICULUM_SOFTWARE_AES=1)
        target_link_libraries(trailmate_reticulum_token_crypto_smoke
            PRIVATE OpenSSL::Crypto)
        target_compile_features(trailmate_reticulum_token_crypto_smoke
            PRIVATE cxx_std_17)
        add_test(NAME trailmate_reticulum_token_crypto_smoke
                 COMMAND trailmate_reticulum_token_crypto_smoke)
    endif()

    add_executable(trailmate_reticulum_runtime_state_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/tests/reticulum_conformance/test_reticulum_runtime_state_contract.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/lxmf/lxmf_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/lxst_call_state_machine.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/lxst_telephony_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/reticulum/reticulum_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_destination_registry.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_delivery_attempt_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_delivery_notifier.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_delivery_planner.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_delivery_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_link_manager.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_link_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_lxst_telephony_client.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_network_page_client.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_packet_router.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_path_manager.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_peer_directory.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_ping_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_propagation_client.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_propagation_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_propagation_service_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_resource_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/src/chat/infra/lxmf/lxmf_transport_runtime.cpp")
    target_include_directories(trailmate_reticulum_runtime_state_contract_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/esp/arduino_common/include")
    # This is a Linux host contract test. Keep ESP task/event-bus integration
    # out of the target even when OpenSSL is available for crypto vectors.
    target_compile_definitions(trailmate_reticulum_runtime_state_contract_smoke
        PRIVATE TRAIL_MATE_RETICULUM_PARSE_ONLY=1)
    if(OpenSSL_FOUND)
        target_compile_definitions(trailmate_reticulum_runtime_state_contract_smoke
            PRIVATE TRAIL_MATE_HAS_OPENSSL=1)
        target_link_libraries(trailmate_reticulum_runtime_state_contract_smoke
            PRIVATE OpenSSL::Crypto)
    endif()
    target_compile_features(trailmate_reticulum_runtime_state_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_reticulum_runtime_state_contract_smoke
             COMMAND trailmate_reticulum_runtime_state_contract_smoke)

    add_executable(trailmate_meshtastic_runtime_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_meshtastic_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_aes_ctr.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_position_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_waypoint_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c"
        ${TRAIL_MATE_MESHTASTIC_GENERATED_SOURCES})
    target_include_directories(trailmate_meshtastic_runtime_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb")
    target_compile_features(trailmate_meshtastic_runtime_smoke
        PRIVATE cxx_std_17)
    target_compile_definitions(trailmate_meshtastic_runtime_smoke
        PRIVATE TRAILMATE_MESHTASTIC_FORCE_SOFTWARE_AES=1)
    add_test(NAME trailmate_meshtastic_runtime_smoke
             COMMAND trailmate_meshtastic_runtime_smoke)

    add_executable(trailmate_meshtastic_radio_config_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_meshtastic_radio_config.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_radio_config.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_region.cpp")
    target_include_directories(trailmate_meshtastic_radio_config_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb")
    target_compile_features(trailmate_meshtastic_radio_config_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshtastic_radio_config_smoke
             COMMAND trailmate_meshtastic_radio_config_smoke)

    add_executable(trailmate_meshtastic_node_payload_smoke
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_uconsole_gtk/tests/uconsole_meshtastic_node_payload_smoke.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_node_payload.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_aes_ctr.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c"
        ${TRAIL_MATE_MESHTASTIC_GENERATED_SOURCES})
    target_include_directories(trailmate_meshtastic_node_payload_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb")
    target_compile_features(trailmate_meshtastic_node_payload_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshtastic_node_payload_smoke
             COMMAND trailmate_meshtastic_node_payload_smoke)

    add_executable(trailmate_meshtastic_waypoint_core_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_meshtastic_waypoint_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_waypoint_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c"
        ${TRAIL_MATE_MESHTASTIC_GENERATED_SOURCES})
    target_include_directories(trailmate_meshtastic_waypoint_core_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb")
    target_compile_features(trailmate_meshtastic_waypoint_core_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshtastic_waypoint_core_smoke
             COMMAND trailmate_meshtastic_waypoint_core_smoke)

    add_executable(trailmate_meshcore_protocol_strategy_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_meshcore_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/meshcore_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/mc_identity_flow.cpp"
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
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/verify.c")
    target_include_directories(trailmate_meshcore_protocol_strategy_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_meshcore_protocol_strategy_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshcore_protocol_strategy_smoke
             COMMAND trailmate_meshcore_protocol_strategy_smoke)

    add_executable(trailmate_receive_packet_service_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_receive_packet_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_dedup_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/peer_identity_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/receive_packet_service.cpp")
    target_include_directories(trailmate_receive_packet_service_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests")
    target_compile_features(trailmate_receive_packet_service_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_receive_packet_service_smoke
             COMMAND trailmate_receive_packet_service_smoke)

    add_executable(trailmate_phone_core_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests/test_phone_core_smoke.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshcore/meshcore_phone_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_session.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_node_payload.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_aes_ctr.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_radio_config.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_region.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/timezone_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
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
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/xmodem.pb.cpp")
    target_include_directories(trailmate_phone_core_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_phone_core_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_phone_core_smoke
             COMMAND trailmate_phone_core_smoke)

    add_executable(trailmate_key_verification_runtime_adapters_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_key_verification_runtime_adapters_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_key_verification_runtime_adapters_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_key_verification_runtime_adapters_smoke
             COMMAND trailmate_key_verification_runtime_adapters_smoke)

    add_executable(trailmate_map_overlay_snapshot_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/tests/test_map_overlay_snapshot_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_overlay_projection_adapter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp")
    target_include_directories(trailmate_map_overlay_snapshot_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    target_compile_features(trailmate_map_overlay_snapshot_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_map_overlay_snapshot_source_smoke
             COMMAND trailmate_map_overlay_snapshot_source_smoke)

    add_executable(trailmate_runtime_async_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_runtime_async.cpp")
    target_include_directories(trailmate_runtime_async_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_runtime_async_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_runtime_async_smoke
             COMMAND trailmate_runtime_async_smoke)

    add_executable(trailmate_app_config_defaults_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_app_config_defaults.cpp")
    target_include_directories(trailmate_app_config_defaults_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    target_compile_definitions(trailmate_app_config_defaults_smoke
        PRIVATE TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22)
    target_compile_features(trailmate_app_config_defaults_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_app_config_defaults_smoke
             COMMAND trailmate_app_config_defaults_smoke)

    add_executable(trailmate_app_config_change_detection_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_app_config_change_detection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp")
    target_include_directories(trailmate_app_config_change_detection_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    target_compile_definitions(trailmate_app_config_change_detection_smoke
        PRIVATE TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22)
    target_compile_features(trailmate_app_config_change_detection_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_app_config_change_detection_smoke
             COMMAND trailmate_app_config_change_detection_smoke)

    add_executable(trailmate_config_persistence_runtime_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_config_persistence_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp")
    target_include_directories(trailmate_config_persistence_runtime_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    target_compile_definitions(trailmate_config_persistence_runtime_smoke
        PRIVATE TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22)
    target_compile_features(trailmate_config_persistence_runtime_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_config_persistence_runtime_smoke
             COMMAND trailmate_config_persistence_runtime_smoke)

    add_executable(trailmate_tms_config_codec_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_tms_config_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/app/tms_config_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp")
    target_include_directories(trailmate_tms_config_codec_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    target_compile_definitions(trailmate_tms_config_codec_smoke
        PRIVATE TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22)
    target_compile_features(trailmate_tms_config_codec_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_tms_config_codec_smoke
             COMMAND trailmate_tms_config_codec_smoke)

    add_executable(trailmate_screen_brightness_steps_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_screen_brightness_steps.cpp")
    target_include_directories(trailmate_screen_brightness_steps_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_screen_brightness_steps_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_screen_brightness_steps_smoke
             COMMAND trailmate_screen_brightness_steps_smoke)

    # MinGW.org's libstdc++ is built without gthreads and does not expose
    # std::mutex. Linux and modern host toolchains exercise this runtime test.
    if(NOT MINGW)
        add_executable(trailmate_reticulum_call_runtime_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_reticulum_call_runtime.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/reticulum_call_runtime.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
        target_include_directories(trailmate_reticulum_call_runtime_smoke
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
        target_compile_features(trailmate_reticulum_call_runtime_smoke
            PRIVATE cxx_std_17)
        add_test(NAME trailmate_reticulum_call_runtime_smoke
                 COMMAND trailmate_reticulum_call_runtime_smoke)
    endif()

    add_executable(trailmate_map_tile_async_runtime_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/tests/test_map_tile_async_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_async_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp")
    target_include_directories(trailmate_map_tile_async_runtime_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_map_tile_async_runtime_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_map_tile_async_runtime_smoke
             COMMAND trailmate_map_tile_async_runtime_smoke)

    add_executable(trailmate_ui_storage_event_runtime_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_ui_storage_event_runtime_contract.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_async_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp")
    target_include_directories(trailmate_ui_storage_event_runtime_contract_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include")
    target_compile_features(trailmate_ui_storage_event_runtime_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_ui_storage_event_runtime_contract_smoke
             COMMAND trailmate_ui_storage_event_runtime_contract_smoke)

    add_executable(trailmate_runtime_map_presentation_adapters_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_runtime_map_presentation_adapters.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/map/map_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_map_workspace_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_runtime_map_presentation_adapters_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_runtime_map_presentation_adapters_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_runtime_map_presentation_adapters_smoke
             COMMAND trailmate_runtime_map_presentation_adapters_smoke)

    add_executable(trailmate_ui_presentation_screen_binding_registry_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_screen_binding_registry.cpp")
    target_link_libraries(trailmate_ui_presentation_screen_binding_registry_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_ui_presentation_screen_binding_registry_smoke
             COMMAND trailmate_ui_presentation_screen_binding_registry_smoke)

    add_executable(trailmate_ime_input_mode_descriptor_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_ime_input_mode_descriptor.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/widgets/ime/ime_input_mode_descriptor.cpp")
    target_include_directories(trailmate_ime_input_mode_descriptor_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/stubs")
    target_compile_features(trailmate_ime_input_mode_descriptor_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_ime_input_mode_descriptor_smoke
             COMMAND trailmate_ime_input_mode_descriptor_smoke)

    add_executable(trailmate_node_info_input_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_node_info_input_contract.cpp")
    target_compile_features(trailmate_node_info_input_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_node_info_input_contract_smoke
             COMMAND trailmate_node_info_input_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_node_info_map_runtime_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_node_info_map_runtime_contract.cpp")
    target_compile_features(trailmate_node_info_map_runtime_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_node_info_map_runtime_contract_smoke
             COMMAND trailmate_node_info_map_runtime_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_wifi_connect_backoff_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_wifi_connect_backoff_contract.cpp")
    target_compile_features(trailmate_wifi_connect_backoff_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_wifi_connect_backoff_contract_smoke
             COMMAND trailmate_wifi_connect_backoff_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_boot_network_independence_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_boot_network_independence_contract.cpp")
    target_compile_features(trailmate_boot_network_independence_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_boot_network_independence_contract_smoke
             COMMAND trailmate_boot_network_independence_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_screen_sleep_wake_race_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_screen_sleep_wake_race_contract.cpp")
    target_compile_features(trailmate_screen_sleep_wake_race_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_screen_sleep_wake_race_contract_smoke
             COMMAND trailmate_screen_sleep_wake_race_contract_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_screen_power_state_machine_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_screen_power_state_machine.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/screen_power_state_machine.cpp")
    target_include_directories(trailmate_screen_power_state_machine_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_screen_power_state_machine_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_screen_power_state_machine_smoke
             COMMAND trailmate_screen_power_state_machine_smoke)

    add_executable(trailmate_tdeck_pro_keyboard_decoder_smoke
        "${TRAIL_MATE_REPO_ROOT}/boards/tdeck_pro/tests/test_tdeck_pro_keyboard_decoder.cpp")
    target_include_directories(trailmate_tdeck_pro_keyboard_decoder_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/boards/tdeck_pro/include")
    target_compile_features(trailmate_tdeck_pro_keyboard_decoder_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_tdeck_pro_keyboard_decoder_smoke
             COMMAND trailmate_tdeck_pro_keyboard_decoder_smoke)

    add_executable(trailmate_network_micron_markup_contract_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_network_micron_markup_contract.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/network/micron_markup_contract.cpp")
    target_include_directories(trailmate_network_micron_markup_contract_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_features(trailmate_network_micron_markup_contract_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_network_micron_markup_contract_smoke
             COMMAND trailmate_network_micron_markup_contract_smoke)

    add_executable(trailmate_network_micron_renderer_fixture_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_network_micron_renderer_fixture.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/network/micron_markup_contract.cpp")
    target_include_directories(trailmate_network_micron_renderer_fixture_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_features(trailmate_network_micron_renderer_fixture_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_network_micron_renderer_fixture_smoke
             COMMAND trailmate_network_micron_renderer_fixture_smoke)

    add_executable(trailmate_pinyin_lookup_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_pinyin_lookup.cpp")
    target_include_directories(trailmate_pinyin_lookup_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_features(trailmate_pinyin_lookup_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_pinyin_lookup_smoke
             COMMAND trailmate_pinyin_lookup_smoke)

    add_executable(trailmate_timezone_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_timezone_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/timezone_profile.cpp")
    target_include_directories(trailmate_timezone_profile_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_timezone_profile_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_timezone_profile_smoke
             COMMAND trailmate_timezone_profile_smoke)

    add_executable(trailmate_battery_estimator_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_battery_estimator.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/power/battery_estimator.cpp")
    target_include_directories(trailmate_battery_estimator_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_battery_estimator_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_battery_estimator_smoke
             COMMAND trailmate_battery_estimator_smoke)

    add_executable(trailmate_team_presence_snapshot_codec_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_presence_and_snapshot_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_persistence/team_ui_snapshot_codec.cpp")
    target_include_directories(trailmate_team_presence_snapshot_codec_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_presence_snapshot_codec_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_presence_snapshot_codec_smoke
             COMMAND trailmate_team_presence_snapshot_codec_smoke)

    add_executable(trailmate_team_mgmt_key_request_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/tests/test_team_mgmt_key_request.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_mgmt.cpp")
    target_include_directories(trailmate_team_mgmt_key_request_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_mgmt_key_request_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_mgmt_key_request_smoke
             COMMAND trailmate_team_mgmt_key_request_smoke)

    add_executable(trailmate_team_app_data_poll_order_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/tests/test_team_app_data_poll_order.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_send_result_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_waypoint.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_mgmt.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/usecase/team_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_team_app_data_poll_order_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_team_app_data_poll_order_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_app_data_poll_order_smoke
             COMMAND trailmate_team_app_data_poll_order_smoke)

    add_executable(trailmate_team_page_read_model_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp")
    target_include_directories(trailmate_team_page_read_model_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_page_read_model_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_read_model_smoke
             COMMAND trailmate_team_page_read_model_smoke)

    add_executable(trailmate_team_page_state_store_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_state_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_state_store.cpp")
    target_include_directories(trailmate_team_page_state_store_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_page_state_store_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_state_store_smoke
             COMMAND trailmate_team_page_state_store_smoke)

    add_executable(trailmate_contacts_team_snapshot_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_contacts_team_snapshot_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_team_snapshot_source.cpp")
    target_include_directories(trailmate_contacts_team_snapshot_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_contacts_team_snapshot_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_contacts_team_snapshot_source_smoke
             COMMAND trailmate_contacts_team_snapshot_source_smoke)

    add_executable(trailmate_team_map_overlay_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_map_overlay_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_map_overlay_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp")
    target_include_directories(trailmate_team_map_overlay_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_map_overlay_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_map_overlay_source_smoke
             COMMAND trailmate_team_map_overlay_source_smoke)

    add_executable(trailmate_team_page_event_reducer_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_event_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp")
    target_include_directories(trailmate_team_page_event_reducer_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_event_reducer_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_event_reducer_smoke
             COMMAND trailmate_team_page_event_reducer_smoke)

    add_executable(trailmate_team_page_command_reducer_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp")
    target_include_directories(trailmate_team_page_command_reducer_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_command_reducer_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_command_reducer_smoke
             COMMAND trailmate_team_page_command_reducer_smoke)

    add_executable(trailmate_team_page_key_event_log_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp")
    target_include_directories(trailmate_team_page_key_event_log_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_key_event_log_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_key_event_log_smoke
             COMMAND trailmate_team_page_key_event_log_smoke)

    add_executable(trailmate_team_page_runtime_port_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_runtime_port.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_runtime_port_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_runtime_port_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_runtime_port_smoke
             COMMAND trailmate_team_page_runtime_port_smoke)

    add_executable(trailmate_team_page_activity_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_activity_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_activity_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_position.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_track.cpp")
    target_include_directories(trailmate_team_page_activity_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_activity_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_activity_sink_smoke
             COMMAND trailmate_team_page_activity_sink_smoke)

    add_executable(trailmate_team_page_deferred_dispatch_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_deferred_dispatch.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch.cpp")
    target_include_directories(trailmate_team_page_deferred_dispatch_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_deferred_dispatch_smoke
        PRIVATE cxx_std_17)
    if(WIN32)
        set_target_properties(trailmate_team_page_deferred_dispatch_smoke
            PROPERTIES OUTPUT_NAME trailmate_team_page_deferred_queue_smoke)
    endif()
    add_test(NAME trailmate_team_page_deferred_dispatch_smoke
             COMMAND trailmate_team_page_deferred_dispatch_smoke)

    add_executable(trailmate_team_page_kick_confirm_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_kick_confirm_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_kick_confirm_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_kick_confirm_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_kick_confirm_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_kick_confirm_action_smoke
             COMMAND trailmate_team_page_kick_confirm_action_smoke)

    add_executable(trailmate_team_page_transfer_leader_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_transfer_leader_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_transfer_leader_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_transfer_leader_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_transfer_leader_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_transfer_leader_action_smoke
             COMMAND trailmate_team_page_transfer_leader_action_smoke)

    add_executable(trailmate_team_page_create_team_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_create_team_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_create_team_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_create_team_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_create_team_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_create_team_action_smoke
             COMMAND trailmate_team_page_create_team_action_smoke)

    add_executable(trailmate_team_page_pairing_command_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_pairing_command_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_pairing_command_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_pairing_command_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_pairing_command_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_pairing_command_action_smoke
             COMMAND trailmate_team_page_pairing_command_action_smoke)

    add_executable(trailmate_team_page_event_effect_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_event_effect_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_effect_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp")
    target_include_directories(trailmate_team_page_event_effect_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_event_effect_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_event_effect_sink_smoke
             COMMAND trailmate_team_page_event_effect_sink_smoke)

    add_executable(trailmate_team_page_request_keys_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_request_keys_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_request_keys_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_request_keys_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_request_keys_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_request_keys_action_smoke
             COMMAND trailmate_team_page_request_keys_action_smoke)

    add_executable(trailmate_team_page_key_request_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_key_request_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_request_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_key_request_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_key_request_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_key_request_action_smoke
             COMMAND trailmate_team_page_key_request_action_smoke)

    add_executable(trailmate_team_page_flow_controller_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_flow_controller.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_flow_controller.cpp")
    target_include_directories(trailmate_team_page_flow_controller_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_page_flow_controller_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_flow_controller_smoke
             COMMAND trailmate_team_page_flow_controller_smoke)

    add_executable(trailmate_team_chat_action_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_team_chat_action_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_chat_action_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_chat_action_sink_smoke
             COMMAND trailmate_team_chat_action_sink_smoke)

    add_executable(trailmate_team_chat_presentation_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presentation/team_rich_payload_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp")
    target_include_directories(trailmate_team_chat_presentation_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_chat_presentation_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_chat_presentation_source_smoke
             COMMAND trailmate_team_chat_presentation_source_smoke)

    add_executable(trailmate_chat_team_workflow_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_chat_team_workflow.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_team_workflow.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/chat/chat_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp")
    target_include_directories(trailmate_chat_team_workflow_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_chat_team_workflow_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_team_workflow_smoke
             COMMAND trailmate_chat_team_workflow_smoke)

    add_executable(trailmate_team_action_runtime_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_action_runtime_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_actions/team_action_runtime_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_team_action_runtime_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_action_runtime_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_action_runtime_sink_smoke
             COMMAND trailmate_team_action_runtime_sink_smoke)

    add_executable(trailmate_team_action_types_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_action_types.cpp")
    target_include_directories(trailmate_team_action_types_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_features(trailmate_team_action_types_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_action_types_smoke
             COMMAND trailmate_team_action_types_smoke)

    foreach(ux_pack_test IN ITEMS
        screen_registry
        input_binding_set
        ux_screen_menu_adapter
        ux_menu_provider
        lvgl_menu_runtime_adapter
        compatibility_screen_factory
        lvgl_screen_host_adapter
        lvgl_screen_graph_bridge
        lvgl_runtime_screen_graph_presenter
        lvgl_runtime_entry_adoption
        lvgl_runtime_adoption_probe
        lvgl_primary_screen_graph_runtime
        lvgl_descriptor_menu_model
        lvgl_descriptor_renderer_probe
        compatibility_menu_binding
        cardputer_compact_ux_pack
        compatibility_ux_pack
        uconsole_desktop_ux_pack
        tiny_node_status_ux_pack
        simulator_full_ux_pack
        ux_pack_registry)
        add_executable(trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/tests/test_${ux_pack_test}.cpp")
        target_link_libraries(trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke
            PRIVATE trailmate_ui_lvgl_ux_packs)
        add_test(NAME trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke
                 COMMAND trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke)
    endforeach()
endif()
