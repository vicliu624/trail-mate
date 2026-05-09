cmake_minimum_required(VERSION 3.24)

project(TrailMateCardputerZeroRpi VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "The Cardputer Zero Pi OS device shell must be configured on Linux.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Shared Linux sources, paths, and helper functions
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/../../cmake/TrailMateLinuxSources.cmake")

# ---------------------------------------------------------------------------
# Common library
# ---------------------------------------------------------------------------

trailmate_add_linux_common(trailmate_cardputer_zero_linux_common)

# ---------------------------------------------------------------------------
# LVGL and UI shell
# ---------------------------------------------------------------------------

include(FetchContent)

set(LV_BUILD_CONF_PATH
    "${PROJECT_SOURCE_DIR}/../../platform/linux/common/include/lv_conf.h"
    CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl)

trailmate_add_linux_ui_shell(
    trailmate_cardputer_zero_ui_shell
    trailmate_cardputer_zero_linux_common
)

# ---------------------------------------------------------------------------
# Device executable – framebuffer target
# ---------------------------------------------------------------------------

add_executable(trailmate_cardputer_zero_device
    "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}/platform/device/linux_framebuffer_platform.cpp"
    "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}/platform/device/evdev_input.cpp"
    src/targets/device_main.cpp
)
target_include_directories(trailmate_cardputer_zero_device
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}"
)
target_link_libraries(trailmate_cardputer_zero_device
    PRIVATE
        trailmate_cardputer_zero_linux_common
        trailmate_cardputer_zero_ui_shell
)
