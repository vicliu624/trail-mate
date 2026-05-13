cmake_minimum_required(VERSION 3.24)

project(TrailMateUConsoleLinux VERSION 0.1.0 LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR
        "The uConsole Linux shell must be configured on Linux.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
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
option(TRAIL_MATE_UCONSOLE_ENABLE_LEGACY_SURFACE
    "Build the LVGL framebuffer fallback backend." OFF)
option(TRAIL_MATE_UCONSOLE_ENABLE_SDL
    "Build the SDL desktop fallback backend." OFF)

if(TRAIL_MATE_UCONSOLE_ENABLE_SDL)
    set(TRAIL_MATE_UCONSOLE_ENABLE_LEGACY_SURFACE ON CACHE BOOL
        "Build the LVGL framebuffer fallback backend." FORCE)
endif()

# ---------------------------------------------------------------------------
# Shared Linux services and source roots
# ---------------------------------------------------------------------------

include("${PROJECT_SOURCE_DIR}/../../cmake/TrailMateLinuxSources.cmake")

trailmate_add_linux_common(trailmate_uconsole_linux_common)

if(TRAIL_MATE_UCONSOLE_ENABLE_LEGACY_SURFACE)
    include(FetchContent)

    # -----------------------------------------------------------------------
    # LVGL - only needed for the legacy framebuffer shell.
    # -----------------------------------------------------------------------

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

    if(TRAIL_MATE_UCONSOLE_ENABLE_SDL)
        # -------------------------------------------------------------------
        # SDL3 - optional LVGL desktop fallback window.
        # -------------------------------------------------------------------

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
    endif()
endif()

# ---------------------------------------------------------------------------
# uConsole GTK app support models
# ---------------------------------------------------------------------------

add_library(trailmate_uconsole_shell
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_gps_status_source.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_map_presentation_source.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_map_action_sink.cpp"
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_chat_workspace_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_dashboard_model.cpp"
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_hardware_probe.cpp"
    "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_map_workspace_model.cpp"
)
if(TRAIL_MATE_UCONSOLE_ENABLE_LEGACY_SURFACE)
    target_sources(trailmate_uconsole_shell
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/uconsole/src/uconsole_desktop_shell.cpp"
    )
endif()
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
        trailmate_uconsole_linux_common
)
if(TRAIL_MATE_UCONSOLE_ENABLE_LEGACY_SURFACE)
    target_link_libraries(trailmate_uconsole_shell PUBLIC lvgl)
    target_compile_definitions(trailmate_uconsole_shell
        PUBLIC TRAIL_MATE_UCONSOLE_HAS_LEGACY_SURFACE=1)
endif()
trailmate_apply_linux_common_warnings(trailmate_uconsole_shell)

# ---------------------------------------------------------------------------
# Smoke tests
# ---------------------------------------------------------------------------

if(BUILD_TESTING)
    add_executable(trailmate_uconsole_map_workspace_smoke
        tests/uconsole_map_workspace_smoke.cpp
    )
    target_link_libraries(trailmate_uconsole_map_workspace_smoke
        PRIVATE trailmate_uconsole_shell
    )
    add_test(NAME trailmate_uconsole_map_workspace_smoke
             COMMAND trailmate_uconsole_map_workspace_smoke)

    add_executable(trailmate_uconsole_chat_dedup_smoke
        tests/uconsole_chat_dedup_smoke.cpp
    )
    target_link_libraries(trailmate_uconsole_chat_dedup_smoke
        PRIVATE trailmate_uconsole_linux_common
    )
    add_test(NAME trailmate_uconsole_chat_dedup_smoke
             COMMAND trailmate_uconsole_chat_dedup_smoke)

    add_executable(trailmate_uconsole_chat_workspace_smoke
        tests/uconsole_chat_workspace_smoke.cpp
    )
    target_link_libraries(trailmate_uconsole_chat_workspace_smoke
        PRIVATE trailmate_uconsole_shell
    )
    add_test(NAME trailmate_uconsole_chat_workspace_smoke
             COMMAND trailmate_uconsole_chat_workspace_smoke)

    add_executable(trailmate_uconsole_meshtastic_node_payload_smoke
        tests/uconsole_meshtastic_node_payload_smoke.cpp
    )
    target_link_libraries(trailmate_uconsole_meshtastic_node_payload_smoke
        PRIVATE trailmate_uconsole_linux_common
    )
    add_test(NAME trailmate_uconsole_meshtastic_node_payload_smoke
             COMMAND trailmate_uconsole_meshtastic_node_payload_smoke)

    add_executable(trailmate_uconsole_chat_sqlite_store_smoke
        tests/uconsole_chat_sqlite_store_smoke.cpp
    )
    target_link_libraries(trailmate_uconsole_chat_sqlite_store_smoke
        PRIVATE trailmate_uconsole_linux_common
    )
    add_test(NAME trailmate_uconsole_chat_sqlite_store_smoke
             COMMAND trailmate_uconsole_chat_sqlite_store_smoke)
endif()

# ---------------------------------------------------------------------------
# Device executable - uConsole desktop window with framebuffer fallback
# ---------------------------------------------------------------------------

set(TRAIL_MATE_UCONSOLE_APP_SOURCES
    src/platform/gtk/gtk_uconsole_app.cpp
    src/platform/gtk/gtk_uconsole_chat_layout.cpp
    src/platform/gtk/gtk_uconsole_chat_logic.cpp
    src/platform/gtk/gtk_uconsole_data_layout.cpp
    src/platform/gtk/gtk_uconsole_data_logic.cpp
    src/platform/gtk/gtk_uconsole_hardware_layout.cpp
    src/platform/gtk/gtk_uconsole_hardware_logic.cpp
    src/platform/gtk/gtk_uconsole_logs_layout.cpp
    src/platform/gtk/gtk_uconsole_logs_logic.cpp
    src/platform/gtk/gtk_uconsole_map_layout.cpp
    src/platform/gtk/gtk_uconsole_map_logic.cpp
    src/platform/gtk/gtk_uconsole_mqtt_settings.cpp
    src/platform/gtk/gtk_uconsole_overview_layout.cpp
    src/platform/gtk/gtk_uconsole_overview_logic.cpp
    src/platform/gtk/gtk_uconsole_pages.cpp
    src/platform/gtk/gtk_uconsole_settings_layout.cpp
    src/platform/gtk/gtk_uconsole_settings_logic.cpp
    src/platform/gtk/gtk_uconsole_shell.cpp
    src/platform/gtk/gtk_uconsole_style.cpp
    src/platform/gtk/gtk_uconsole_widgets.cpp
    "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}/platform/device/evdev_input.cpp"
    src/targets/uconsole_main.cpp
)
if(TRAIL_MATE_UCONSOLE_ENABLE_LEGACY_SURFACE)
    list(APPEND TRAIL_MATE_UCONSOLE_APP_SOURCES
        "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}/platform/device/linux_framebuffer_platform.cpp"
    )
endif()
if(TRAIL_MATE_UCONSOLE_ENABLE_SDL)
    list(APPEND TRAIL_MATE_UCONSOLE_APP_SOURCES
        src/platform/desktop/sdl_window_presenter.cpp
    )
endif()

add_executable(trailmate_uconsole ${TRAIL_MATE_UCONSOLE_APP_SOURCES})
target_include_directories(trailmate_uconsole
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${TRAIL_MATE_LINUX_RPI_SRC_ROOT}"
)
target_link_libraries(trailmate_uconsole
    PRIVATE
        PkgConfig::GTK4
        trailmate_uconsole_shell
)
if(TRAIL_MATE_UCONSOLE_ENABLE_LEGACY_SURFACE)
    target_compile_definitions(trailmate_uconsole
        PRIVATE TRAIL_MATE_UCONSOLE_HAS_LEGACY_SURFACE=1)
endif()
if(TRAIL_MATE_UCONSOLE_ENABLE_SDL)
    target_compile_definitions(trailmate_uconsole
        PRIVATE TRAIL_MATE_UCONSOLE_HAS_SDL=1)
    target_link_libraries(trailmate_uconsole PRIVATE SDL3::SDL3)
endif()
set_target_properties(trailmate_uconsole
    PROPERTIES
        OUTPUT_NAME "trailmate-uconsole"
)

add_executable(trailmate_sx1262_probe
    tools/sx1262_probe.cpp
)
target_link_libraries(trailmate_sx1262_probe
    PRIVATE
        trailmate_uconsole_linux_common
)
set_target_properties(trailmate_sx1262_probe
    PROPERTIES
        OUTPUT_NAME "trailmate-sx1262-probe"
)

# ---------------------------------------------------------------------------
# Install and Debian package
# ---------------------------------------------------------------------------

install(TARGETS trailmate_uconsole
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
install(TARGETS trailmate_sx1262_probe
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
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libc6, libstdc++6, libgcc-s1, libssl3, gdal-bin, unzip, ca-certificates")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Trail Mate uConsole GTK desktop shell with raw framebuffer fallback.")

include(CPack)
