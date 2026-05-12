cmake_minimum_required(VERSION 3.24)

project(TrailMateCardputerZeroSimulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
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
# SDL3 – simulator-only dependency
# ---------------------------------------------------------------------------

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

add_executable(trailmate_cardputer_zero_simulator
    src/platform/simulator/sdl_simulator.cpp
    src/targets/simulator_main.cpp
)
target_include_directories(trailmate_cardputer_zero_simulator PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(trailmate_cardputer_zero_simulator
    PRIVATE trailmate_cardputer_zero_linux_common SDL3::SDL3
)

# ---------------------------------------------------------------------------
# LVGL and UI shell
# ---------------------------------------------------------------------------

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
# Simulator-specific include path for shell_ui_runner internals
target_include_directories(trailmate_cardputer_zero_ui_shell
    PRIVATE "${PROJECT_SOURCE_DIR}/src"
)

target_link_libraries(trailmate_cardputer_zero_simulator
    PRIVATE trailmate_cardputer_zero_ui_shell
)

# ---------------------------------------------------------------------------
# Smoke tests (simulator-only)
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
    add_executable(trailmate_cardputer_zero_smoke tests/demo_app_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_smoke
             COMMAND trailmate_cardputer_zero_smoke)

    add_executable(trailmate_cardputer_zero_runtime_smoke
                   tests/linux_runtime_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_runtime_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_runtime_smoke
             COMMAND trailmate_cardputer_zero_runtime_smoke)

    add_executable(trailmate_cardputer_zero_path_safety_smoke
                   tests/path_safety_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_path_safety_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_path_safety_smoke
             COMMAND trailmate_cardputer_zero_path_safety_smoke)

    add_executable(trailmate_core_phone_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests/test_phone_core_smoke.cpp"
    )
    target_include_directories(trailmate_core_phone_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests")
    target_link_libraries(trailmate_core_phone_smoke
        PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_core_phone_smoke
             COMMAND trailmate_core_phone_smoke)

    add_executable(trailmate_core_gps_nmea_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/tests/test_nmea_parser.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_parser.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_sentence.cpp"
    )
    target_include_directories(trailmate_core_gps_nmea_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    add_test(NAME trailmate_core_gps_nmea_smoke
             COMMAND trailmate_core_gps_nmea_smoke)

    add_executable(trailmate_core_gps_location_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/tests/test_location_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_parser.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_sentence.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/gps_jitter_filter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/location_service.cpp"
    )
    target_include_directories(trailmate_core_gps_location_smoke
        PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    add_test(NAME trailmate_core_gps_location_smoke
             COMMAND trailmate_core_gps_location_smoke)

    add_executable(trailmate_core_gps_time_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/tests/test_time_authority.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/usecase/time_authority.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
    )
    target_include_directories(trailmate_core_gps_time_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    add_test(NAME trailmate_core_gps_time_smoke
             COMMAND trailmate_core_gps_time_smoke)

    set(TRAIL_MATE_CORE_MESH_TEST_SOURCES
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/meshcore_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshtastic/meshtastic_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/direct_message_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_dedup_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_session.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/peer_identity_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/receive_packet_service.cpp"
    )

    foreach(core_mesh_test
            peer_identity_service
            direct_message_service
            receive_packet_service
            mesh_session)
        add_executable(trailmate_core_mesh_${core_mesh_test}_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_${core_mesh_test}.cpp"
            ${TRAIL_MATE_CORE_MESH_TEST_SOURCES}
        )
        target_include_directories(trailmate_core_mesh_${core_mesh_test}_smoke
            PRIVATE
                "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
                "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests")
        add_test(NAME trailmate_core_mesh_${core_mesh_test}_smoke
                 COMMAND trailmate_core_mesh_${core_mesh_test}_smoke)
    endforeach()
endif()
