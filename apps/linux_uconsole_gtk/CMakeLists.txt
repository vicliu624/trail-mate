cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxUConsoleGtkShell LANGUAGES CXX)

include(CTest)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

# Transitional Adapter delegates; legacy implementation still runs.
add_library(trailmate_linux_uconsole_legacy_adapter
    "${TRAIL_MATE_REPO_ROOT}/legacy/app_implementations/linux_uconsole/src/uconsole_legacy_implementation_adapter.cpp")

target_include_directories(trailmate_linux_uconsole_legacy_adapter
    PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/legacy/app_implementations/linux_uconsole/src")

target_compile_features(trailmate_linux_uconsole_legacy_adapter PUBLIC cxx_std_17)

add_library(trailmate_linux_uconsole_gtk_shell
    src/linux_uconsole_gtk_app_shell.cpp
    src/linux_uconsole_gtk_page_registry_adoption.cpp
    src/linux_uconsole_gtk_runtime_entry_adoption_probe.cpp
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_menu_runtime_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_screen_host_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_bridge.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_uconsole_screen_graph_presenter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/src/gtk_runtime_entry_adoption.cpp")

target_include_directories(trailmate_linux_uconsole_gtk_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/include")

target_link_libraries(trailmate_linux_uconsole_gtk_shell
    PUBLIC
        trailmate_linux_uconsole_legacy_adapter
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_linux_uconsole_gtk_shell PUBLIC cxx_std_17)

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

    add_executable(trailmate_linux_uconsole_gtk_page_registry_fallback_smoke
        tests/linux_uconsole_gtk_page_registry_fallback_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_page_registry_fallback_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_page_registry_fallback_smoke
             COMMAND trailmate_linux_uconsole_gtk_page_registry_fallback_smoke)

    add_executable(trailmate_gtk_runtime_entry_adoption_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_gtk_runtime/tests/gtk_runtime_entry_adoption_smoke.cpp")
    target_link_libraries(trailmate_gtk_runtime_entry_adoption_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_gtk_runtime_entry_adoption_smoke
             COMMAND trailmate_gtk_runtime_entry_adoption_smoke)

    add_executable(trailmate_linux_uconsole_legacy_adapter_smoke
        "${TRAIL_MATE_REPO_ROOT}/legacy/app_implementations/linux_uconsole/tests/uconsole_legacy_implementation_adapter_smoke.cpp")
    target_link_libraries(trailmate_linux_uconsole_legacy_adapter_smoke
        PRIVATE trailmate_linux_uconsole_legacy_adapter)
    add_test(NAME trailmate_linux_uconsole_legacy_adapter_smoke
             COMMAND trailmate_linux_uconsole_legacy_adapter_smoke)
endif()
