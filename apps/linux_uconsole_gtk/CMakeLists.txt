cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxUConsoleGtkShell LANGUAGES C CXX)

include(CTest)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

set(TRAIL_MATE_UCONSOLE_PACKAGE_VERSION "0.1.0"
    CACHE STRING "Debian package version for trailmate-uconsole")
option(TRAIL_MATE_UCONSOLE_BUILD_REAL_GTK_UI
    "Build the restored uConsole GTK executable when Linux GTK4 is available."
    ON)

set(TRAIL_MATE_UCONSOLE_REAL_GTK_SOURCES
    src/platform/gtk/gtk_uconsole_app.cpp
    src/platform/gtk/gtk_uconsole_chat_layout.cpp
    src/platform/gtk/gtk_uconsole_chat_logic.cpp
    src/platform/gtk/gtk_uconsole_data_layout.cpp
    src/platform/gtk/gtk_uconsole_data_logic.cpp
    src/platform/gtk/gtk_uconsole_hardware_layout.cpp
    src/platform/gtk/gtk_uconsole_hardware_logic.cpp
    src/platform/gtk/gtk_uconsole_logs_layout.cpp
    src/platform/gtk/gtk_uconsole_logs_logic.cpp
    src/platform/gtk/gtk_uconsole_map_layout.cpp
    src/platform/gtk/gtk_uconsole_map_logic.cpp
    src/platform/gtk/gtk_uconsole_mqtt_settings.cpp
    src/platform/gtk/gtk_uconsole_overview_layout.cpp
    src/platform/gtk/gtk_uconsole_overview_logic.cpp
    src/platform/gtk/gtk_uconsole_pages.cpp
    src/platform/gtk/gtk_uconsole_settings_layout.cpp
    src/platform/gtk/gtk_uconsole_settings_logic.cpp
    src/platform/gtk/gtk_uconsole_shell.cpp
    src/platform/gtk/gtk_uconsole_style.cpp
    src/platform/gtk/gtk_uconsole_widgets.cpp)

add_library(trailmate_linux_uconsole_gtk_shell
    src/linux_uconsole_gtk_app_shell.cpp
    src/linux_uconsole_gtk_historical_source_descriptor.cpp
    src/linux_uconsole_gtk_page_registry_adoption.cpp
    src/linux_uconsole_gtk_page_registry_renderer.cpp
    src/linux_uconsole_gtk_runtime_entry_adoption_probe.cpp
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_ux_binding.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_menu_runtime_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_screen_host_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_bridge.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_presenter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_runtime_entry_adoption.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_descriptor_page_registry.cpp")

target_include_directories(trailmate_linux_uconsole_gtk_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/include")

target_link_libraries(trailmate_linux_uconsole_gtk_shell
    PUBLIC
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_linux_uconsole_gtk_shell PUBLIC cxx_std_17)

if(TRAIL_MATE_UCONSOLE_BUILD_REAL_GTK_UI AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GTK4 QUIET IMPORTED_TARGET gtk4)
    endif()

    if(GTK4_FOUND)
        include(GNUInstallDirs)
        include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxSources.cmake")
        find_package(Threads REQUIRED)

        trailmate_add_linux_common(trailmate_uconsole_linux_common)

        add_library(trailmate_uconsole_runtime_models
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_gps_status_source.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_map_presentation_source.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_map_action_sink.cpp"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_chat_workspace_model.cpp"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_dashboard_model.cpp"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_hardware_probe.cpp"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_map_workspace_model.cpp")
        target_include_directories(trailmate_uconsole_runtime_models
            PUBLIC
                "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/include"
                "${TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT}"
                "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}")
        target_compile_features(trailmate_uconsole_runtime_models PUBLIC cxx_std_20)
        target_compile_definitions(trailmate_uconsole_runtime_models
            PRIVATE TRAIL_MATE_UCONSOLE_LINUX=1)
        target_link_libraries(trailmate_uconsole_runtime_models
            PUBLIC trailmate_uconsole_linux_common)
        trailmate_apply_linux_common_warnings(trailmate_uconsole_runtime_models)

        add_executable(trailmate_uconsole
            ${TRAIL_MATE_UCONSOLE_REAL_GTK_SOURCES}
            src/targets/uconsole_main.cpp)
        target_include_directories(trailmate_uconsole
            PRIVATE
                "${CMAKE_CURRENT_LIST_DIR}/src"
                "${TRAIL_MATE_REPO_ROOT}/platform/linux/rpi/src")
        target_link_libraries(trailmate_uconsole
            PRIVATE
                PkgConfig::GTK4
                trailmate_uconsole_runtime_models
                trailmate_linux_uconsole_gtk_shell)
        target_compile_features(trailmate_uconsole PRIVATE cxx_std_20)
        set_target_properties(trailmate_uconsole
            PROPERTIES OUTPUT_NAME "trailmate-uconsole")

        add_executable(trailmate_sx1262_probe
            tools/sx1262_probe.cpp)
        target_link_libraries(trailmate_sx1262_probe
            PRIVATE trailmate_uconsole_linux_common)
        target_compile_features(trailmate_sx1262_probe PRIVATE cxx_std_20)
        set_target_properties(trailmate_sx1262_probe
            PROPERTIES OUTPUT_NAME "trailmate-sx1262-probe")

        if(BUILD_TESTING)
            add_executable(trailmate_uconsole_map_workspace_smoke
                tests/uconsole_map_workspace_smoke.cpp)
            target_link_libraries(trailmate_uconsole_map_workspace_smoke
                PRIVATE trailmate_uconsole_runtime_models)
            add_test(NAME trailmate_uconsole_map_workspace_smoke
                     COMMAND trailmate_uconsole_map_workspace_smoke)

            add_executable(trailmate_uconsole_chat_dedup_smoke
                tests/uconsole_chat_dedup_smoke.cpp)
            target_link_libraries(trailmate_uconsole_chat_dedup_smoke
                PRIVATE trailmate_uconsole_linux_common)
            add_test(NAME trailmate_uconsole_chat_dedup_smoke
                     COMMAND trailmate_uconsole_chat_dedup_smoke)

            add_executable(trailmate_uconsole_chat_workspace_smoke
                tests/uconsole_chat_workspace_smoke.cpp)
            target_link_libraries(trailmate_uconsole_chat_workspace_smoke
                PRIVATE trailmate_uconsole_runtime_models)
            add_test(NAME trailmate_uconsole_chat_workspace_smoke
                     COMMAND trailmate_uconsole_chat_workspace_smoke)

            add_executable(trailmate_uconsole_meshtastic_node_payload_smoke
                tests/uconsole_meshtastic_node_payload_smoke.cpp)
            target_link_libraries(trailmate_uconsole_meshtastic_node_payload_smoke
                PRIVATE trailmate_uconsole_linux_common)
            add_test(NAME trailmate_uconsole_meshtastic_node_payload_smoke
                     COMMAND trailmate_uconsole_meshtastic_node_payload_smoke)

            add_executable(trailmate_uconsole_chat_sqlite_store_smoke
                tests/uconsole_chat_sqlite_store_smoke.cpp)
            target_link_libraries(trailmate_uconsole_chat_sqlite_store_smoke
                PRIVATE trailmate_uconsole_linux_common)
            add_test(NAME trailmate_uconsole_chat_sqlite_store_smoke
                     COMMAND trailmate_uconsole_chat_sqlite_store_smoke)
        endif()

        install(TARGETS trailmate_uconsole trailmate_sx1262_probe
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
        install(FILES README.md
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/trailmate-uconsole")
        install(FILES packaging/trailmate-uconsole.desktop
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
        install(FILES packaging/trailmate-uconsole.png
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps")
        install(FILES packaging/trailmate-uconsole.png
            DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps")

        set(CPACK_GENERATOR "DEB")
        set(CPACK_PACKAGE_NAME "trailmate-uconsole")
        set(CPACK_PACKAGE_VENDOR "Trail Mate")
        set(CPACK_PACKAGE_CONTACT "Trail Mate Maintainers")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
            "Trail Mate uConsole Linux desktop shell")
        set(CPACK_PACKAGE_VERSION "${TRAIL_MATE_UCONSOLE_PACKAGE_VERSION}")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Trail Mate Maintainers")
        set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libc6, libstdc++6, libgcc-s1, libssl3, gdal-bin, unzip, ca-certificates")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
        set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
            "Trail Mate uConsole GTK desktop shell.")
        include(CPack)
    else()
        message(STATUS "GTK4 was not found; restored uConsole GTK executable is not built on this host.")
    endif()
else()
    message(STATUS "Restored uConsole GTK executable is enabled only on Linux GTK4 hosts.")
endif()

if(BUILD_TESTING)
    add_executable(trailmate_linux_uconsole_gtk_app_shell_smoke
        tests/linux_uconsole_gtk_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_app_shell_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_app_shell_smoke
             COMMAND trailmate_linux_uconsole_gtk_app_shell_smoke)

    add_executable(trailmate_linux_uconsole_gtk_historical_source_descriptor_smoke
        tests/linux_uconsole_gtk_historical_source_descriptor_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_historical_source_descriptor_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_historical_source_descriptor_smoke
             COMMAND trailmate_linux_uconsole_gtk_historical_source_descriptor_smoke)

    add_executable(trailmate_linux_uconsole_gtk_runtime_entry_adoption_probe_smoke
        tests/linux_uconsole_gtk_runtime_entry_adoption_probe_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_runtime_entry_adoption_probe_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_runtime_entry_adoption_probe_smoke
             COMMAND trailmate_linux_uconsole_gtk_runtime_entry_adoption_probe_smoke)

    add_executable(trailmate_linux_uconsole_gtk_page_registry_adoption_smoke
        tests/linux_uconsole_gtk_page_registry_adoption_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_page_registry_adoption_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_page_registry_adoption_smoke
             COMMAND trailmate_linux_uconsole_gtk_page_registry_adoption_smoke)

    add_executable(trailmate_linux_uconsole_gtk_page_registry_fallback_smoke
        tests/linux_uconsole_gtk_page_registry_fallback_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_page_registry_fallback_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_page_registry_fallback_smoke
             COMMAND trailmate_linux_uconsole_gtk_page_registry_fallback_smoke)

    add_executable(trailmate_linux_uconsole_gtk_page_registry_renderer_smoke
        tests/linux_uconsole_gtk_page_registry_renderer_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_page_registry_renderer_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_page_registry_renderer_smoke
             COMMAND trailmate_linux_uconsole_gtk_page_registry_renderer_smoke)

    add_executable(trailmate_gtk_runtime_entry_adoption_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/tests/gtk_runtime_entry_adoption_smoke.cpp")
    target_link_libraries(trailmate_gtk_runtime_entry_adoption_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_gtk_runtime_entry_adoption_smoke
             COMMAND trailmate_gtk_runtime_entry_adoption_smoke)

    add_executable(trailmate_gtk_descriptor_page_registry_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/tests/test_gtk_descriptor_page_registry.cpp")
    target_link_libraries(trailmate_gtk_descriptor_page_registry_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_gtk_descriptor_page_registry_smoke
             COMMAND trailmate_gtk_descriptor_page_registry_smoke)

endif()
