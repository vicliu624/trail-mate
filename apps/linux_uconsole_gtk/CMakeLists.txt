cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxUConsoleGtkShell LANGUAGES CXX)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

add_library(trailmate_linux_uconsole_gtk_shell
    src/linux_uconsole_gtk_app_shell.cpp)

target_include_directories(trailmate_linux_uconsole_gtk_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src")

target_compile_features(trailmate_linux_uconsole_gtk_shell PUBLIC cxx_std_17)

if(BUILD_TESTING)
    add_executable(trailmate_linux_uconsole_gtk_app_shell_smoke
        tests/linux_uconsole_gtk_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_uconsole_gtk_app_shell_smoke
        PRIVATE trailmate_linux_uconsole_gtk_shell)
    add_test(NAME trailmate_linux_uconsole_gtk_app_shell_smoke
             COMMAND trailmate_linux_uconsole_gtk_app_shell_smoke)
endif()
