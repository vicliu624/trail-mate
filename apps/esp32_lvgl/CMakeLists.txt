cmake_minimum_required(VERSION 3.20)

project(TrailMateEsp32LvglAppShell LANGUAGES CXX)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

add_library(trailmate_esp_idf_legacy_adapter
    "${CMAKE_CURRENT_LIST_DIR}/../esp_idf/src/esp_idf_legacy_implementation_adapter.cpp")

target_include_directories(trailmate_esp_idf_legacy_adapter
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/../esp_idf/src")

target_compile_features(trailmate_esp_idf_legacy_adapter PUBLIC cxx_std_17)

add_library(trailmate_esp32_lvgl_app_shell
    src/esp32_lvgl_app_shell.cpp)

target_include_directories(trailmate_esp32_lvgl_app_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src")

target_link_libraries(trailmate_esp32_lvgl_app_shell
    PUBLIC
        trailmate_esp_idf_legacy_adapter
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_esp32_lvgl_app_shell PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(trailmate_esp32_lvgl_app_shell_smoke
        tests/esp32_lvgl_app_shell_smoke.cpp)
    target_link_libraries(trailmate_esp32_lvgl_app_shell_smoke
        PRIVATE trailmate_esp32_lvgl_app_shell)
    add_test(NAME trailmate_esp32_lvgl_app_shell_smoke
             COMMAND trailmate_esp32_lvgl_app_shell_smoke)
endif()
