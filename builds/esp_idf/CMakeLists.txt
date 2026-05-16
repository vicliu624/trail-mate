cmake_minimum_required(VERSION 3.20)

project(TrailMateEspIdfBuildWrapper LANGUAGES CXX)

# Build Entrypoint invokes; App Shell composes.
set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

set(TRAIL_MATE_ESP_APP_SHELL
    "${TRAIL_MATE_REPO_ROOT}/apps/esp32_lvgl"
    CACHE PATH "Trail Mate ESP32 LVGL app shell")

include("${CMAKE_CURRENT_LIST_DIR}/ESP_IDF_COMPONENT_SOURCES.cmake")

include(CTest)

# ESP-IDF physical component registration may still run from the historical
# component root during migration. Component source ownership now lives in
# ESP_IDF_COMPONENT_SOURCES.cmake and the final app/platform/module owners.
add_subdirectory(
    "${TRAIL_MATE_ESP_APP_SHELL}"
    "${CMAKE_BINARY_DIR}/apps/esp32_lvgl")
