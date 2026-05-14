cmake_minimum_required(VERSION 3.20)

project(TrailMateEspIdfBuildWrapper LANGUAGES CXX)

# Build Entrypoint invokes; App Shell composes.
set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

set(TRAIL_MATE_ESP_APP_SHELL
    "${TRAIL_MATE_REPO_ROOT}/apps/esp32_lvgl"
    CACHE PATH "Trail Mate ESP32 LVGL app shell")

include(CTest)

# Phase 8 convergence baseline:
# ESP-IDF physical component registration remains in apps/esp_idf.
# This wrapper establishes the authority direction without duplicating IDF
# component registration or runtime object assembly.
add_subdirectory(
    "${TRAIL_MATE_ESP_APP_SHELL}"
    "${CMAKE_BINARY_DIR}/apps/esp32_lvgl")
