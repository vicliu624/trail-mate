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

include(CTest)

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
