cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxUConsoleGtkShell LANGUAGES C CXX)

include(CTest)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

set(TRAIL_MATE_UCONSOLE_PACKAGE_VERSION "0.1.38-alpha"
    CACHE STRING "Debian package version for trailmate-uconsole")
option(TRAIL_MATE_UCONSOLE_BUILD_REAL_GTK_UI
    "Build the restored uConsole GTK executable when Linux GTK4 is available."
    ON)
option(TRAIL_MATE_UCONSOLE_BUILD_SDL_SCREENSHOT_CAPTURE
    "Build the SDL-rendered uConsole screenshot capture tool."
    OFF)
option(TRAIL_MATE_UCONSOLE_BUILD_SDL_UI
    "Build the SDL-rendered uConsole desktop executable."
    OFF)
set(TRAIL_MATE_SDL3_SOURCE_DIR ""
    CACHE PATH "Optional SDL3 source tree used when SDL3 is not installed.")

function(trailmate_add_uconsole_lvgl)
    if(TARGET lvgl)
        return()
    endif()

    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(LV_BUILD_CONF_DIR
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
        CACHE PATH "LVGL configuration directory for uConsole Linux shells"
        FORCE)
    set(TRAIL_MATE_LOCAL_LVGL_DIR
        "${TRAIL_MATE_REPO_ROOT}/managed_components/lvgl__lvgl")
    if(EXISTS "${TRAIL_MATE_LOCAL_LVGL_DIR}/CMakeLists.txt")
        add_subdirectory(
            "${TRAIL_MATE_LOCAL_LVGL_DIR}"
            "${CMAKE_BINARY_DIR}/managed_components/lvgl__lvgl"
            EXCLUDE_FROM_ALL)
    else()
        include(FetchContent)
        FetchContent_Declare(lvgl
            GIT_REPOSITORY https://github.com/lvgl/lvgl.git
            GIT_TAG v9.4.0
            GIT_SHALLOW TRUE)
        FetchContent_GetProperties(lvgl)
        if(NOT lvgl_POPULATED)
            FetchContent_Populate(lvgl)
        endif()
        add_subdirectory(
            "${lvgl_SOURCE_DIR}"
            "${lvgl_BINARY_DIR}"
            EXCLUDE_FROM_ALL)
    endif()
endfunction()

set(TRAIL_MATE_UCONSOLE_REAL_GTK_SOURCES
    src/platform/gtk/gtk_uconsole_app.cpp
    src/platform/gtk/gtk_uconsole_chat_layout.cpp
    src/platform/gtk/gtk_uconsole_chat_logic.cpp
    src/platform/gtk/gtk_uconsole_data_layout.cpp
    src/platform/gtk/gtk_uconsole_data_logic.cpp
    src/platform/gtk/gtk_uconsole_field_pages.cpp
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
    src/platform/gtk/gtk_uconsole_radio_tools.cpp
    src/platform/gtk/gtk_uconsole_settings_layout.cpp
    src/platform/gtk/gtk_uconsole_settings_logic.cpp
    src/platform/gtk/gtk_uconsole_shell.cpp
    src/platform/gtk/gtk_uconsole_style.cpp
    src/platform/gtk/gtk_uconsole_widgets.cpp)

add_library(trailmate_linux_uconsole_gtk_shell
    src/linux_uconsole_gtk_app_shell.cpp
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

        # Linux app services still expose shared LVGL-compatible runtime types;
        # the production uConsole executable itself is GTK-only.
        trailmate_add_uconsole_lvgl()
        trailmate_add_linux_common(trailmate_uconsole_linux_common)

        add_library(trailmate_uconsole_runtime_models
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_gps_status_source.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_map_workspace_source.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_position_core.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_waypoint_core.cpp"
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

        target_sources(trailmate_uconsole_linux_common
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_position_core.cpp"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_waypoint_core.cpp")

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
                trailmate_uconsole_runtime_models)
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

            add_executable(trailmate_uconsole_hardware_probe_smoke
                tests/uconsole_hardware_probe_smoke.cpp)
            target_link_libraries(trailmate_uconsole_hardware_probe_smoke
                PRIVATE trailmate_uconsole_runtime_models)
            add_test(NAME trailmate_uconsole_hardware_probe_smoke
                     COMMAND trailmate_uconsole_hardware_probe_smoke)

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

            add_executable(trailmate_uconsole_protocol_switch_smoke
                tests/uconsole_protocol_switch_smoke.cpp)
            target_link_libraries(trailmate_uconsole_protocol_switch_smoke
                PRIVATE trailmate_uconsole_linux_common)
            add_test(NAME trailmate_uconsole_protocol_switch_smoke
                     COMMAND trailmate_uconsole_protocol_switch_smoke)

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

if(TRAIL_MATE_UCONSOLE_BUILD_SDL_UI OR
   TRAIL_MATE_UCONSOLE_BUILD_SDL_SCREENSHOT_CAPTURE)
    if(NOT TARGET SDL3::SDL3 AND
       TRAIL_MATE_SDL3_SOURCE_DIR AND
       EXISTS "${TRAIL_MATE_SDL3_SOURCE_DIR}/CMakeLists.txt")
        set(SDL_TESTS OFF CACHE BOOL "" FORCE)
        set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
        add_subdirectory("${TRAIL_MATE_SDL3_SOURCE_DIR}"
                         "${CMAKE_BINARY_DIR}/SDL3"
                         EXCLUDE_FROM_ALL)
    endif()
    find_package(Threads REQUIRED)
    if(NOT TARGET SDL3::SDL3)
        find_package(SDL3 CONFIG REQUIRED)
    endif()

    trailmate_add_uconsole_lvgl()

    include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxSources.cmake")
    trailmate_add_linux_common(trailmate_uconsole_sdl_linux_common)
    target_sources(trailmate_uconsole_sdl_linux_common
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_message_ledger.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_outbox_service.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/read/chat_read_state_ledger.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_position_core.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/runtime/meshtastic_waypoint_core.cpp")

    add_library(trailmate_uconsole_sdl_shell
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_chat_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_dashboard_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_desktop_shell.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_hardware_probe.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_map_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_gps_status_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_map_workspace_source.cpp")
    target_include_directories(trailmate_uconsole_sdl_shell
        PUBLIC
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/include"
            "${TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT}"
            "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}")
    target_compile_features(trailmate_uconsole_sdl_shell PUBLIC cxx_std_20)
    target_compile_definitions(trailmate_uconsole_sdl_shell
        PRIVATE TRAIL_MATE_UCONSOLE_LINUX=1)
    target_link_libraries(trailmate_uconsole_sdl_shell
        PUBLIC
            lvgl
            trailmate_uconsole_sdl_linux_common)
    trailmate_apply_linux_common_warnings(trailmate_uconsole_sdl_shell)

    if(TRAIL_MATE_UCONSOLE_BUILD_SDL_UI)
        add_executable(trailmate_uconsole_sdl
            src/platform/desktop/sdl_window_presenter.cpp
            src/targets/uconsole_sdl_main.cpp)
        target_include_directories(trailmate_uconsole_sdl
            PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
        target_compile_features(trailmate_uconsole_sdl PRIVATE cxx_std_20)
        target_link_libraries(trailmate_uconsole_sdl
            PRIVATE SDL3::SDL3 trailmate_uconsole_sdl_shell)
        set_target_properties(trailmate_uconsole_sdl
            PROPERTIES OUTPUT_NAME "trailmate-uconsole-sdl")
        install(TARGETS trailmate_uconsole_sdl
            RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
    endif()

if(TRAIL_MATE_UCONSOLE_BUILD_SDL_SCREENSHOT_CAPTURE)
    add_executable(trailmate_uconsole_sdl_screenshot_capture
        src/platform/desktop/sdl_window_presenter.cpp
        tools/uconsole_sdl_screenshot_capture.cpp)
    target_include_directories(trailmate_uconsole_sdl_screenshot_capture
        PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src")
    target_compile_features(trailmate_uconsole_sdl_screenshot_capture
        PRIVATE cxx_std_20)
    target_link_libraries(trailmate_uconsole_sdl_screenshot_capture
        PRIVATE
            SDL3::SDL3
            trailmate_uconsole_sdl_shell)
endif()
endif()

if(BUILD_TESTING)
    add_executable(trailmate_linux_uconsole_gtk_app_shell_smoke
        tests/linux_uconsole_gtk_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_app_shell_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_app_shell_smoke
             COMMAND trailmate_linux_uconsole_gtk_app_shell_smoke)

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

    add_executable(trailmate_uconsole_page_manifest_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_page_manifest.cpp")
    target_link_libraries(trailmate_uconsole_page_manifest_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_uconsole_page_manifest_smoke
             COMMAND trailmate_uconsole_page_manifest_smoke)

endif()
