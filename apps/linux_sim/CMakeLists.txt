cmake_minimum_required(VERSION 3.24)

project(TrailMateCardputerZeroSimulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Shared Linux sources, paths, and helper functions
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/../../cmake/TrailMateLinuxSources.cmake")

# ---------------------------------------------------------------------------
# Common library
# ---------------------------------------------------------------------------

trailmate_add_linux_common(trailmate_cardputer_zero_linux_common)

# ---------------------------------------------------------------------------
# SDL3 – simulator-only dependency
# ---------------------------------------------------------------------------

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

add_executable(trailmate_cardputer_zero_simulator
    src/platform/simulator/sdl_simulator.cpp
    src/targets/simulator_main.cpp
)
target_include_directories(trailmate_cardputer_zero_simulator PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(trailmate_cardputer_zero_simulator
    PRIVATE trailmate_cardputer_zero_linux_common SDL3::SDL3
)

# ---------------------------------------------------------------------------
# LVGL and UI shell
# ---------------------------------------------------------------------------

set(LV_BUILD_CONF_PATH
    "${PROJECT_SOURCE_DIR}/../../platform/linux/common/include/lv_conf.h"
    CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl)

trailmate_add_linux_ui_shell(
    trailmate_cardputer_zero_ui_shell
    trailmate_cardputer_zero_linux_common
)
# Simulator-specific include path for shell_ui_runner internals
target_include_directories(trailmate_cardputer_zero_ui_shell
    PRIVATE "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(trailmate_cardputer_zero_simulator
    PRIVATE trailmate_cardputer_zero_ui_shell
)

# ---------------------------------------------------------------------------
# Smoke tests (simulator-only)
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
    add_executable(trailmate_cardputer_zero_smoke tests/demo_app_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_smoke
             COMMAND trailmate_cardputer_zero_smoke)

    add_executable(trailmate_cardputer_zero_runtime_smoke
                   tests/linux_runtime_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_runtime_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_runtime_smoke
             COMMAND trailmate_cardputer_zero_runtime_smoke)

    add_executable(trailmate_cardputer_zero_path_safety_smoke
                   tests/path_safety_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_path_safety_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_path_safety_smoke
             COMMAND trailmate_cardputer_zero_path_safety_smoke)

    add_executable(trailmate_core_phone_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests/test_phone_core_smoke.cpp"
    )
    target_include_directories(trailmate_core_phone_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests")
    target_link_libraries(trailmate_core_phone_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_core_phone_smoke
             COMMAND trailmate_core_phone_smoke)

    add_executable(trailmate_ui_presentation_common_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_fixed_text.cpp"
    )
    target_include_directories(trailmate_ui_presentation_common_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    add_test(NAME trailmate_ui_presentation_common_smoke
             COMMAND trailmate_ui_presentation_common_smoke)

    add_executable(trailmate_ui_presentation_device_status_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_device_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/device/device_status_model.cpp"
    )
    target_include_directories(trailmate_ui_presentation_device_status_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_device_status_smoke
             COMMAND trailmate_ui_presentation_device_status_smoke)

    add_executable(trailmate_ui_presentation_gps_status_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_gps_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/gps/gps_status_model.cpp"
    )
    target_include_directories(trailmate_ui_presentation_gps_status_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_gps_status_smoke
             COMMAND trailmate_ui_presentation_gps_status_smoke)

    add_executable(trailmate_ui_presentation_mesh_status_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_mesh_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/mesh/mesh_status_model.cpp"
    )
    target_include_directories(trailmate_ui_presentation_mesh_status_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_mesh_status_smoke
             COMMAND trailmate_ui_presentation_mesh_status_smoke)

    add_executable(trailmate_ui_presentation_settings_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_settings_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/settings/settings_model.cpp"
    )
    target_include_directories(trailmate_ui_presentation_settings_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_settings_smoke
             COMMAND trailmate_ui_presentation_settings_smoke)

    add_executable(trailmate_ui_presentation_chat_identity_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_chat_presentation_identity.cpp"
    )
    target_include_directories(trailmate_ui_presentation_chat_identity_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    add_test(NAME trailmate_ui_presentation_chat_identity_smoke
             COMMAND trailmate_ui_presentation_chat_identity_smoke)

    add_executable(trailmate_ui_presentation_chat_workspace_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_chat_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/chat/chat_workspace_model.cpp"
    )
    target_include_directories(trailmate_ui_presentation_chat_workspace_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_chat_workspace_smoke
             COMMAND trailmate_ui_presentation_chat_workspace_smoke)

    add_executable(trailmate_ui_presentation_map_workspace_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_map_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/map/map_workspace_model.cpp"
    )
    target_include_directories(trailmate_ui_presentation_map_workspace_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_map_workspace_smoke
             COMMAND trailmate_ui_presentation_map_workspace_smoke)

    add_executable(trailmate_ui_presentation_workspace_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_presentation_workspace.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/device/device_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/gps/gps_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/mesh/mesh_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/settings/settings_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/chat/chat_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/map/map_workspace_model.cpp"
    )
    target_include_directories(trailmate_ui_presentation_workspace_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_workspace_smoke
             COMMAND trailmate_ui_presentation_workspace_smoke)

    add_executable(trailmate_ui_presentation_workspace_probe_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_presentation_workspace_probe.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/device/device_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/gps/gps_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/mesh/mesh_status_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/settings/settings_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/chat/chat_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/map/map_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/workspace/presentation_workspace_probe.cpp"
    )
    target_include_directories(trailmate_ui_presentation_workspace_probe_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_ui_presentation_workspace_probe_smoke
             COMMAND trailmate_ui_presentation_workspace_probe_smoke)

    add_executable(trailmate_map_workspace_ascii_probe
        tests/map_workspace_ascii_probe.cpp
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/map/map_workspace_model.cpp"
    )
    target_include_directories(trailmate_map_workspace_ascii_probe
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests")
    add_test(NAME trailmate_map_workspace_ascii_probe
             COMMAND trailmate_map_workspace_ascii_probe)

    add_executable(trailmate_chat_presentation_mapper_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/tests/test_chat_conversation_mapper.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_conversation_mapper.cpp"
    )
    target_include_directories(trailmate_chat_presentation_mapper_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    add_test(NAME trailmate_chat_presentation_mapper_smoke
             COMMAND trailmate_chat_presentation_mapper_smoke)

    add_executable(trailmate_chat_message_mapper_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/tests/test_chat_message_mapper.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_message_mapper.cpp"
    )
    target_include_directories(trailmate_chat_message_mapper_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    add_test(NAME trailmate_chat_message_mapper_smoke
             COMMAND trailmate_chat_message_mapper_smoke)

    add_executable(trailmate_legacy_chat_presentation_adapters_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_legacy_chat_presentation_adapters.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_conversation_mapper.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_message_mapper.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
    )
    target_include_directories(trailmate_legacy_chat_presentation_adapters_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    add_test(NAME trailmate_legacy_chat_presentation_adapters_smoke
             COMMAND trailmate_legacy_chat_presentation_adapters_smoke)

    add_executable(trailmate_team_chat_presentation_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
    )
    target_include_directories(trailmate_team_chat_presentation_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    add_test(NAME trailmate_team_chat_presentation_source_smoke
             COMMAND trailmate_team_chat_presentation_source_smoke)

    add_executable(trailmate_team_chat_action_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
    )
    target_include_directories(trailmate_team_chat_action_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    add_test(NAME trailmate_team_chat_action_sink_smoke
             COMMAND trailmate_team_chat_action_sink_smoke)

    add_executable(trailmate_legacy_map_presentation_adapters_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_legacy_map_presentation_adapters.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_map_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_map_action_sink.cpp"
    )
    target_include_directories(trailmate_legacy_map_presentation_adapters_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    add_test(NAME trailmate_legacy_map_presentation_adapters_smoke
             COMMAND trailmate_legacy_map_presentation_adapters_smoke)

    add_executable(trailmate_core_gps_nmea_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/tests/test_nmea_parser.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_parser.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_sentence.cpp"
    )
    target_include_directories(trailmate_core_gps_nmea_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    add_test(NAME trailmate_core_gps_nmea_smoke
             COMMAND trailmate_core_gps_nmea_smoke)

    add_executable(trailmate_core_gps_location_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/tests/test_location_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_parser.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_sentence.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/gps_jitter_filter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/location_service.cpp"
    )
    target_include_directories(trailmate_core_gps_location_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    add_test(NAME trailmate_core_gps_location_smoke
             COMMAND trailmate_core_gps_location_smoke)

    add_executable(trailmate_core_gps_time_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/tests/test_time_authority.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/time_authority.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
    )
    target_include_directories(trailmate_core_gps_time_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    add_test(NAME trailmate_core_gps_time_smoke
             COMMAND trailmate_core_gps_time_smoke)

    set(TRAIL_MATE_CORE_MESH_TEST_SOURCES
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/direct_message_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_dedup_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_session.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/peer_identity_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/receive_packet_service.cpp"
    )

    foreach(core_mesh_test
            store_contracts
            peer_identity_service
            direct_message_service
            receive_packet_service
            mesh_session)
        add_executable(trailmate_core_mesh_${core_mesh_test}_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_${core_mesh_test}.cpp"
            ${TRAIL_MATE_CORE_MESH_TEST_SOURCES}
        )
        target_include_directories(trailmate_core_mesh_${core_mesh_test}_smoke
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests")
        add_test(NAME trailmate_core_mesh_${core_mesh_test}_smoke
                 COMMAND trailmate_core_mesh_${core_mesh_test}_smoke)
    endforeach()

    set(TRAIL_MATE_MESHTASTIC_PROTOCOL_TEST_SOURCES
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshtastic/meshtastic_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_radio_config.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_region.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/channel.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/config.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/device_ui.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/mesh.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/module_config.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/portnums.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/telemetry.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/xmodem.pb.cpp"
    )

    add_executable(trailmate_core_mesh_meshtastic_protocol_strategy_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_meshtastic_protocol_strategy.cpp"
        ${TRAIL_MATE_MESHTASTIC_PROTOCOL_TEST_SOURCES}
    )
    target_include_directories(trailmate_core_mesh_meshtastic_protocol_strategy_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb")
    add_test(NAME trailmate_core_mesh_meshtastic_protocol_strategy_smoke
             COMMAND trailmate_core_mesh_meshtastic_protocol_strategy_smoke)

    set(TRAIL_MATE_MESHCORE_PROTOCOL_TEST_SOURCES
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/meshcore_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/mc_region_presets.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_payload_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers.cpp"
    )

    add_executable(trailmate_core_mesh_meshcore_protocol_strategy_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_meshcore_protocol_strategy.cpp"
        ${TRAIL_MATE_MESHCORE_PROTOCOL_TEST_SOURCES}
    )
    target_include_directories(trailmate_core_mesh_meshcore_protocol_strategy_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    add_test(NAME trailmate_core_mesh_meshcore_protocol_strategy_smoke
             COMMAND trailmate_core_mesh_meshcore_protocol_strategy_smoke)
endif()
