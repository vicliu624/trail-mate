function(trailmate_add_ui_lvgl_ux_packs target_name)
    if(TARGET ${target_name})
        return()
    endif()

    if(NOT DEFINED TRAIL_MATE_REPO_ROOT)
        set(TRAIL_MATE_REPO_ROOT
            "${CMAKE_CURRENT_LIST_DIR}/.."
            CACHE PATH "Trail Mate repository root")
    endif()

    add_library(${target_name}
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/menu/menu_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/screen/screen_binding_registry.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/target_ui_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/page/page_manifest.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/layout/layout_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/screen_registry.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/input_binding_set.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/ux_menu_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/ux_screen_menu_adapter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/ux_menu_provider.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/ux/ux_pack_registry.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/compatibility_screen_factory.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_menu_runtime_adapter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_host_adapter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_screen_graph_bridge.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_screen_graph_presenter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_entry_adoption.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_runtime_adoption_probe.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_primary_screen_graph_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_menu_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/runtime/lvgl_descriptor_renderer_probe.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/packs/compatibility_ux_pack.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/packs/uconsole_desktop_ux_pack.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/packs/tiny_node_status_ux_pack.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/src/packs/simulator_full_ux_pack.cpp")

    target_include_directories(${target_name}
        PUBLIC
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/include")

    target_compile_features(${target_name} PUBLIC cxx_std_17)
endfunction()
