cmake_minimum_required(VERSION 3.24)

project(TrailMateUConsoleLinux VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "The uConsole Linux shell must be configured on Linux.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(TRAIL_MATE_UCONSOLE_PACKAGE_VERSION "${PROJECT_VERSION}"
    CACHE STRING "Debian package version for trailmate-uconsole")
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK4 REQUIRED IMPORTED_TARGET gtk4)
include(GNUInstallDirs)
set(TRAIL_MATE_UCONSOLE_DOCDIR
    "${CMAKE_INSTALL_DATAROOTDIR}/doc/trailmate-uconsole")
set(CMAKE_INSTALL_DOCDIR "${TRAIL_MATE_UCONSOLE_DOCDIR}")
set(CMAKE_INSTALL_DOCDIR "${TRAIL_MATE_UCONSOLE_DOCDIR}"
    CACHE PATH "Documentation install directory" FORCE)

# ---------------------------------------------------------------------------
# Shared Linux services and source roots
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/../../cmake/TrailMateLinuxSources.cmake")

trailmate_add_linux_common(trailmate_uconsole_linux_common)

# ---------------------------------------------------------------------------
# SDL3 - desktop window shell
# ---------------------------------------------------------------------------

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    sdl3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.4
    GIT_SHALLOW TRUE
)

FetchContent_GetProperties(sdl3)
if(NOT sdl3_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(sdl3)
    add_subdirectory("${sdl3_SOURCE_DIR}" "${sdl3_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# LVGL
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

FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD)
    endif()
    FetchContent_Populate(lvgl)
    add_subdirectory("${lvgl_SOURCE_DIR}" "${lvgl_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

# ---------------------------------------------------------------------------
# uConsole desktop shell
# ---------------------------------------------------------------------------

add_library(trailmate_uconsole_shell
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_chat_workspace_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_dashboard_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_desktop_shell.cpp"
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_map_workspace_model.cpp"
)
target_include_directories(trailmate_uconsole_shell
    PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/include"
        "${TRAIL_MATE_LINUX_COMMON_INCLUDE_ROOT}"
        "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}"
)
target_compile_features(trailmate_uconsole_shell PUBLIC cxx_std_20)
target_compile_definitions(trailmate_uconsole_shell
    PRIVATE
        TRAIL_MATE_UCONSOLE_LINUX=1
)
target_link_libraries(trailmate_uconsole_shell
    PUBLIC
        lvgl
        trailmate_uconsole_linux_common
)
trailmate_apply_linux_common_warnings(trailmate_uconsole_shell)

# ---------------------------------------------------------------------------
# Device executable - uConsole desktop window with framebuffer fallback
# ---------------------------------------------------------------------------

add_executable(trailmate_uconsole
    src/platform/gtk/gtk_uconsole_app.cpp
    src/platform/desktop/sdl_window_presenter.cpp
    "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}/platform/device/linux_framebuffer_platform.cpp"
    "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}/platform/device/evdev_input.cpp"
    src/targets/uconsole_main.cpp
)
target_include_directories(trailmate_uconsole
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}"
)
target_link_libraries(trailmate_uconsole
    PRIVATE
        PkgConfig::GTK4
        SDL3::SDL3
        trailmate_uconsole_shell
)
set_target_properties(trailmate_uconsole
    PROPERTIES
        OUTPUT_NAME "trailmate-uconsole"
)

# ---------------------------------------------------------------------------
# Install and Debian package
# ---------------------------------------------------------------------------

install(TARGETS trailmate_uconsole
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(FILES README.md
    DESTINATION "${CMAKE_INSTALL_DOCDIR}"
)
install(FILES packaging/trailmate-uconsole.desktop
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
)
install(FILES packaging/trailmate-uconsole.png
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/pixmaps"
)
install(FILES packaging/trailmate-uconsole.png
    DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps"
)

set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "trailmate-uconsole")
set(CPACK_PACKAGE_VENDOR "Trail Mate")
set(CPACK_PACKAGE_CONTACT "Trail Mate Maintainers")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Trail Mate uConsole Linux desktop shell")
set(CPACK_PACKAGE_VERSION "${TRAIL_MATE_UCONSOLE_PACKAGE_VERSION}")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Trail Mate Maintainers")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libc6, libstdc++6, libgcc-s1")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Trail Mate uConsole GTK desktop shell with raw framebuffer fallback.")

include(CPack)
