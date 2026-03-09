cmake_minimum_required(VERSION 3.16.0)

set(TRAIL_MATE_IDF_TARGET "tab5" CACHE STRING "Active ESP-IDF target")
set_property(CACHE TRAIL_MATE_IDF_TARGET PROPERTY STRINGS tab5 t_display_p4)

set(TRAIL_MATE_IDF_TARGET_DIR "${CMAKE_SOURCE_DIR}/apps/esp_idf/targets/${TRAIL_MATE_IDF_TARGET}")
if(NOT EXISTS "${TRAIL_MATE_IDF_TARGET_DIR}/sdkconfig.defaults")
    message(FATAL_ERROR "Missing sdkconfig.defaults for TRAIL_MATE_IDF_TARGET='${TRAIL_MATE_IDF_TARGET}' at ${TRAIL_MATE_IDF_TARGET_DIR}")
endif()

# Keep generated sdkconfig files inside the selected build directory so
# different IDF targets/build trees do not fight over stale source-root
# sdkconfig.<target> artifacts.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig.${TRAIL_MATE_IDF_TARGET}")
set(SDKCONFIG_DEFAULTS "${TRAIL_MATE_IDF_TARGET_DIR}/sdkconfig.defaults")

# `apps/esp_idf` is now the shared ESP-IDF component root.
# Board choice stays explicit via `TRAIL_MATE_IDF_TARGET`, while board-specific
# runtime logic continues to live under `platform/esp/boards/<board>`.
message(STATUS "trail-mate IDF target: ${TRAIL_MATE_IDF_TARGET}")
message(STATUS "trail-mate SDKCONFIG path: ${SDKCONFIG}")
set(EXTRA_COMPONENT_DIRS
    apps/esp_idf
    platform/esp/idf_components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(trail-mate)
