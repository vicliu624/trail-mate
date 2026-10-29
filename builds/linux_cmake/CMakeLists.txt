cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxBuilds LANGUAGES CXX)

# Build Entrypoint invokes; App Shell composes.
set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

option(TRAIL_MATE_BUILD_LINUX_UCONSOLE_GTK
       "Build Linux uConsole GTK app shell" ON)
option(TRAIL_MATE_BUILD_LINUX_SIM_SHELL
       "Build Linux simulator app shell" ON)
option(TRAIL_MATE_BUILD_LINUX_CARDPUTER_ZERO
       "Build Linux Cardputer Zero app shell" ON)
option(TRAIL_MATE_BUILD_LINUX_LVGL
       "Build the configurable Linux LVGL desktop shell" OFF)

include(CTest)

if(TRAIL_MATE_BUILD_LINUX_LVGL)
    add_subdirectory(
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_lvgl"
        "${CMAKE_BINARY_DIR}/apps/linux_lvgl")
endif()

if(TRAIL_MATE_BUILD_LINUX_UCONSOLE_GTK)
    add_subdirectory(
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_uconsole_gtk"
        "${CMAKE_BINARY_DIR}/apps/linux_uconsole_gtk")
endif()

if(TRAIL_MATE_BUILD_LINUX_SIM_SHELL)
    add_subdirectory(
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_sim_shell"
        "${CMAKE_BINARY_DIR}/apps/linux_sim_shell")
endif()

if(TRAIL_MATE_BUILD_LINUX_CARDPUTER_ZERO)
    add_subdirectory(
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_cardputer_zero"
        "${CMAKE_BINARY_DIR}/apps/linux_cardputer_zero")
endif()
