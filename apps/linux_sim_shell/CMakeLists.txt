cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxSimShell LANGUAGES CXX)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

# Transitional Adapter delegates; legacy implementation still runs.
add_library(trailmate_linux_sim_legacy_adapter
    "${TRAIL_MATE_REPO_ROOT}/apps/linux_sim/src/linux_sim_legacy_implementation_adapter.cpp")

target_include_directories(trailmate_linux_sim_legacy_adapter
    PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_sim/src")

target_compile_features(trailmate_linux_sim_legacy_adapter PUBLIC cxx_std_17)

add_library(trailmate_linux_sim_shell
    src/linux_sim_app_shell.cpp)

target_include_directories(trailmate_linux_sim_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src")

target_link_libraries(trailmate_linux_sim_shell
    PUBLIC trailmate_linux_sim_legacy_adapter)

target_compile_features(trailmate_linux_sim_shell PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(trailmate_linux_sim_app_shell_smoke
        tests/linux_sim_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_app_shell_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_app_shell_smoke
             COMMAND trailmate_linux_sim_app_shell_smoke)

    add_executable(trailmate_linux_sim_legacy_adapter_smoke
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_sim/tests/linux_sim_legacy_implementation_adapter_smoke.cpp")
    target_link_libraries(trailmate_linux_sim_legacy_adapter_smoke
        PRIVATE trailmate_linux_sim_legacy_adapter)
    add_test(NAME trailmate_linux_sim_legacy_adapter_smoke
             COMMAND trailmate_linux_sim_legacy_adapter_smoke)
endif()
