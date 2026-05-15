cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxSimShell LANGUAGES CXX)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

# Transitional Adapter delegates; legacy implementation still runs.
add_library(trailmate_linux_sim_legacy_adapter
    "${TRAIL_MATE_REPO_ROOT}/legacy/app_implementations/linux_sim/src/linux_sim_legacy_implementation_adapter.cpp")

target_include_directories(trailmate_linux_sim_legacy_adapter
    PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/legacy/app_implementations/linux_sim/src")

target_compile_features(trailmate_linux_sim_legacy_adapter PUBLIC cxx_std_17)

add_library(trailmate_linux_sim_shell
    src/linux_sim_app_shell.cpp)

target_include_directories(trailmate_linux_sim_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src")

target_link_libraries(trailmate_linux_sim_shell
    PUBLIC
        trailmate_linux_sim_legacy_adapter
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_linux_sim_shell PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(trailmate_linux_sim_app_shell_smoke
        tests/linux_sim_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_app_shell_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_app_shell_smoke
             COMMAND trailmate_linux_sim_app_shell_smoke)

    add_executable(trailmate_linux_sim_legacy_adapter_smoke
        "${TRAIL_MATE_REPO_ROOT}/legacy/app_implementations/linux_sim/tests/linux_sim_legacy_implementation_adapter_smoke.cpp")
    target_link_libraries(trailmate_linux_sim_legacy_adapter_smoke
        PRIVATE trailmate_linux_sim_legacy_adapter)
    add_test(NAME trailmate_linux_sim_legacy_adapter_smoke
             COMMAND trailmate_linux_sim_legacy_adapter_smoke)

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
