cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxSimShell LANGUAGES CXX)

include(CTest)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

add_library(trailmate_linux_sim_shell
    src/linux_sim_app_shell.cpp
    src/linux_sim_historical_source_descriptor.cpp
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
    add_executable(trailmate_linux_sim_app_shell_smoke
        tests/linux_sim_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_app_shell_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_app_shell_smoke
             COMMAND trailmate_linux_sim_app_shell_smoke)

    add_executable(trailmate_linux_sim_historical_source_descriptor_smoke
        tests/linux_sim_historical_source_descriptor_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_historical_source_descriptor_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_historical_source_descriptor_smoke
             COMMAND trailmate_linux_sim_historical_source_descriptor_smoke)

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

    add_executable(trailmate_linux_sim_runtime_entry_fallback_smoke
        tests/linux_sim_runtime_entry_fallback_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_entry_fallback_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_entry_fallback_smoke
             COMMAND trailmate_linux_sim_runtime_entry_fallback_smoke)

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

    add_executable(trailmate_legacy_chat_delivery_action_bridge_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_chat_delivery_action_bridge_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_legacy_chat_delivery_action_bridge_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_chat_delivery_action_bridge_alias_smoke
             COMMAND trailmate_legacy_chat_delivery_action_bridge_alias_smoke)

    add_executable(trailmate_legacy_chat_delivery_event_bridge_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_chat_delivery_event_bridge_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_legacy_chat_delivery_event_bridge_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_chat_delivery_event_bridge_alias_smoke
             COMMAND trailmate_legacy_chat_delivery_event_bridge_alias_smoke)

    add_executable(trailmate_key_verification_runtime_adapters_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp"
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

    add_executable(trailmate_legacy_key_verification_adapters_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_key_verification_adapters_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_legacy_key_verification_adapters_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_key_verification_adapters_alias_smoke
             COMMAND trailmate_legacy_key_verification_adapters_alias_smoke)

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

    add_executable(trailmate_legacy_map_overlay_source_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_map_overlay_source_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    target_compile_features(trailmate_legacy_map_overlay_source_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_map_overlay_source_alias_smoke
             COMMAND trailmate_legacy_map_overlay_source_alias_smoke)

    add_executable(trailmate_ui_presentation_screen_binding_registry_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_screen_binding_registry.cpp")
    target_link_libraries(trailmate_ui_presentation_screen_binding_registry_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_ui_presentation_screen_binding_registry_smoke
             COMMAND trailmate_ui_presentation_screen_binding_registry_smoke)

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
