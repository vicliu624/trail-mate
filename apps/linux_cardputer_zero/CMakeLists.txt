cmake_minimum_required(VERSION 3.20)
project(TrailMateLinuxCardputerZero LANGUAGES C CXX)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/TrailMateTmapProducer.cmake")
trailmate_install_tmap_producer(trailmate-cardputer-zero)

include(CTest)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateProjectVersion.cmake")
set(TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION "${TRAIL_MATE_DEBIAN_PROJECT_VERSION}"
    CACHE STRING "Debian package version for trailmate-cardputer-zero")
option(TRAIL_MATE_CARDPUTER_ZERO_BUILD_SCREENSHOT_CAPTURE
       "Build the Cardputer Zero shared-LVGL screenshot capture tool."
       ON)
option(TRAIL_MATE_CARDPUTER_ZERO_BUILD_DEVICE
       "Build the Cardputer Zero Wayland device executable and fbdev fallback."
       ON)
option(TRAIL_MATE_CARDPUTER_ZERO_ENABLE_DEB_PACKAGE
       "Enable the Cardputer Zero CPack Debian package target."
       OFF)

add_library(trailmate_linux_cardputer_zero_shell
    src/cardputer_zero_input_method_port.cpp
    src/cardputer_zero_notification_port.cpp
    src/linux_cardputer_zero_app_shell.cpp
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_ux_binding.cpp")

target_include_directories(trailmate_linux_cardputer_zero_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${TRAIL_MATE_REPO_ROOT}"
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/include")

target_link_libraries(trailmate_linux_cardputer_zero_shell
    PUBLIC
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_linux_cardputer_zero_shell PUBLIC cxx_std_17)

if(TRAIL_MATE_CARDPUTER_ZERO_BUILD_SCREENSHOT_CAPTURE OR TRAIL_MATE_CARDPUTER_ZERO_BUILD_DEVICE)
    find_package(Threads REQUIRED)

    if(NOT TARGET lvgl)
        set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
        set(LV_BUILD_CONF_DIR
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
            CACHE PATH "LVGL configuration directory for Linux Cardputer Zero"
            FORCE)
        set(TRAIL_MATE_LOCAL_LVGL_DIR
            "${TRAIL_MATE_REPO_ROOT}/managed_components/lvgl__lvgl")
        if(EXISTS "${TRAIL_MATE_LOCAL_LVGL_DIR}/CMakeLists.txt")
            add_subdirectory(
                "${TRAIL_MATE_LOCAL_LVGL_DIR}"
                "${CMAKE_BINARY_DIR}/managed_components/lvgl__lvgl"
                EXCLUDE_FROM_ALL)
        else()
            include(FetchContent)
            FetchContent_Declare(lvgl
                GIT_REPOSITORY https://github.com/lvgl/lvgl.git
                GIT_TAG v9.4.0
                GIT_SHALLOW TRUE)
            FetchContent_GetProperties(lvgl)
            if(NOT lvgl_POPULATED)
                FetchContent_Populate(lvgl)
            endif()
            add_subdirectory(
                "${lvgl_SOURCE_DIR}"
                "${lvgl_BINARY_DIR}"
                EXCLUDE_FROM_ALL)
        endif()
    endif()

    include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxSources.cmake")
    trailmate_add_linux_common(
        trailmate_cardputer_zero_linux_common ENABLE_MEDIA)
    trailmate_add_linux_ui_shell(
        trailmate_cardputer_zero_ui_shell
        trailmate_cardputer_zero_linux_common)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux"
       AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
        set(cardputer_zero_arm64_asset_sources "")
        foreach(source_path IN LISTS TRAIL_MATE_LINUX_UI_SHELL_SOURCES)
            if(source_path MATCHES "/modules/ui_shared/src/ui/assets/.*\\.c$")
                list(APPEND cardputer_zero_arm64_asset_sources "${source_path}")
            endif()
        endforeach()
        if(cardputer_zero_arm64_asset_sources)
            set_source_files_properties(
                ${cardputer_zero_arm64_asset_sources}
                PROPERTIES COMPILE_OPTIONS "-O0")
        endif()
    endif()
endif()

if(TRAIL_MATE_CARDPUTER_ZERO_BUILD_DEVICE AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(GNUInstallDirs)
    set(TRAIL_MATE_CARDPUTER_ZERO_PRIVATE_LIBDIR
        "lib/trailmate-cardputer-zero")
    set(CMAKE_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
        OWNER_READ OWNER_WRITE OWNER_EXECUTE
        GROUP_READ GROUP_EXECUTE
        WORLD_READ WORLD_EXECUTE)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
    pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
    pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
    find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner REQUIRED)

    pkg_get_variable(TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_PROTOCOLS_DIR
        wayland-protocols pkgdatadir)
    set(TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR
        "${CMAKE_BINARY_DIR}/generated/cardputer_zero_wayland")
    set(TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_XML
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
    set(TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_XML
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_PROTOCOLS_DIR}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
    set(TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_HEADER
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}/xdg-shell-client-protocol.h")
    set(TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_SOURCE
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.c")
    set(TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_HEADER
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}/xdg-decoration-unstable-v1-client-protocol.h")
    set(TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_SOURCE
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}/xdg-decoration-unstable-v1-protocol.c")

    if(NOT EXISTS "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_XML}")
        message(FATAL_ERROR
            "xdg-shell.xml not found at ${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_XML}")
    endif()
    if(NOT EXISTS "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_XML}")
        message(FATAL_ERROR
            "xdg-decoration-unstable-v1.xml not found at ${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_XML}")
    endif()

    file(MAKE_DIRECTORY "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}")
    set(TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_XML
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_PROTOCOLS_DIR}/unstable/text-input/text-input-unstable-v3.xml")
    set(TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_HEADER
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}/text-input-unstable-v3-client-protocol.h")
    set(TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_SOURCE
        "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}/text-input-unstable-v3-protocol.c")
    add_custom_command(OUTPUT "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_HEADER}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
            "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_XML}" "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_HEADER}"
        DEPENDS "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_XML}" VERBATIM)
    add_custom_command(OUTPUT "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_SOURCE}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
            "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_XML}" "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_SOURCE}"
        DEPENDS "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_XML}" VERBATIM)
    add_custom_command(
        OUTPUT "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_HEADER}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_XML}"
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_HEADER}"
        DEPENDS "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_XML}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_SOURCE}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_XML}"
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_SOURCE}"
        DEPENDS "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_XML}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_HEADER}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_XML}"
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_HEADER}"
        DEPENDS "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_XML}"
        VERBATIM)
    add_custom_command(
        OUTPUT "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_SOURCE}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_XML}"
                "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_SOURCE}"
        DEPENDS "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_XML}"
        VERBATIM)

    add_library(trailmate_cardputer_zero_wayland_protocol STATIC
        "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_SOURCE}"
        "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_HEADER}"
        "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_SOURCE}"
        "${TRAIL_MATE_CARDPUTER_ZERO_XDG_SHELL_HEADER}"
        "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_SOURCE}"
        "${TRAIL_MATE_CARDPUTER_ZERO_XDG_DECORATION_HEADER}")
    target_include_directories(trailmate_cardputer_zero_wayland_protocol
        PUBLIC "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}")
    target_link_libraries(trailmate_cardputer_zero_wayland_protocol
        PUBLIC PkgConfig::WAYLAND_CLIENT)
    if(BUILD_TESTING)
        pkg_check_modules(WAYLAND_SERVER REQUIRED IMPORTED_TARGET wayland-server)
        set(TRAIL_MATE_TEXT_INPUT_SERVER_HEADER "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}/text-input-unstable-v3-server-protocol.h")
        add_custom_command(OUTPUT "${TRAIL_MATE_TEXT_INPUT_SERVER_HEADER}"
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" server-header
                "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_XML}" "${TRAIL_MATE_TEXT_INPUT_SERVER_HEADER}"
            DEPENDS "${TRAIL_MATE_CARDPUTER_ZERO_TEXT_INPUT_XML}" VERBATIM)
        add_executable(trailmate_linux_wayland_text_input_test tests/linux_wayland_text_input_test.cpp
            "${TRAIL_MATE_TEXT_INPUT_SERVER_HEADER}")
        target_include_directories(trailmate_linux_wayland_text_input_test PRIVATE "${CMAKE_CURRENT_LIST_DIR}/src"
            "${TRAIL_MATE_LINUX_COMMON_SRC_ROOT}")
        target_link_libraries(trailmate_linux_wayland_text_input_test PRIVATE
            trailmate_cardputer_zero_wayland_protocol PkgConfig::WAYLAND_SERVER)
        target_compile_options(trailmate_linux_wayland_text_input_test PRIVATE -UNDEBUG)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT CMAKE_CROSSCOMPILING)
            target_compile_options(trailmate_linux_wayland_text_input_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(trailmate_linux_wayland_text_input_test PRIVATE -fsanitize=address,undefined)
        endif()
        add_test(NAME trailmate_linux_wayland_text_input COMMAND trailmate_linux_wayland_text_input_test)
    endif()

    add_executable(trailmate_cardputer_zero_wayland
        src/cardputer_zero_wayland_main.cpp
        src/platform/wayland/wayland_presenter.cpp)
    target_include_directories(trailmate_cardputer_zero_wayland
        PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/src"
            "${TRAIL_MATE_CARDPUTER_ZERO_WAYLAND_GENERATED_DIR}")
    target_link_libraries(trailmate_cardputer_zero_wayland
        PRIVATE
            trailmate_cardputer_zero_ui_shell
            trailmate_cardputer_zero_wayland_protocol
            PkgConfig::WAYLAND_CLIENT
            PkgConfig::XKBCOMMON
            rt)
    target_compile_features(trailmate_cardputer_zero_wayland PRIVATE cxx_std_20)
    set_target_properties(trailmate_cardputer_zero_wayland
        PROPERTIES OUTPUT_NAME "trailmate-cardputer-zero")

    add_executable(trailmate_cardputer_zero_fbdev
        src/cardputer_zero_main.cpp
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/rpi/src/platform/device/evdev_input.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/rpi/src/platform/device/linux_framebuffer_platform.cpp")
    target_include_directories(trailmate_cardputer_zero_fbdev
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/rpi/src")
    target_link_libraries(trailmate_cardputer_zero_fbdev
        PRIVATE trailmate_cardputer_zero_ui_shell)
    target_compile_features(trailmate_cardputer_zero_fbdev PRIVATE cxx_std_20)
    set_target_properties(trailmate_cardputer_zero_fbdev
        PROPERTIES OUTPUT_NAME "trailmate-cardputer-zero-fbdev")

    install(TARGETS trailmate_cardputer_zero_wayland trailmate_cardputer_zero_fbdev
        RUNTIME DESTINATION "${TRAIL_MATE_CARDPUTER_ZERO_PRIVATE_LIBDIR}"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    configure_file(packaging/trailmate-cardputer-zero-applaunch
        "${CMAKE_CURRENT_BINARY_DIR}/trailmate-cardputer-zero-applaunch"
        @ONLY NEWLINE_STYLE UNIX)
    install(PROGRAMS "${CMAKE_CURRENT_BINARY_DIR}/trailmate-cardputer-zero-applaunch"
        DESTINATION "${TRAIL_MATE_CARDPUTER_ZERO_PRIVATE_LIBDIR}"
        PERMISSIONS
            OWNER_READ OWNER_WRITE OWNER_EXECUTE
            GROUP_READ GROUP_EXECUTE
            WORLD_READ WORLD_EXECUTE)
    install(FILES README.md
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/trailmate-cardputer-zero"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    install(FILES "${TRAIL_MATE_REPO_ROOT}/third_party/bzip2/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/trailmate-cardputer-zero"
        RENAME copyright-bzip2
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    install(FILES "${TRAIL_MATE_REPO_ROOT}/third_party/rweather_crypto/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/doc/trailmate-cardputer-zero"
        RENAME copyright-rweather-Crypto
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    install(FILES packaging/trailmate-cardputer-zero.desktop
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/APPLaunch/applications"
        RENAME trailmate.desktop
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
    install(FILES packaging/trailmate-cardputer-zero.png
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/APPLaunch/share/images"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

    if(TRAIL_MATE_CARDPUTER_ZERO_ENABLE_DEB_PACKAGE)
        set(CPACK_GENERATOR "DEB")
        set(CPACK_PACKAGE_NAME "trailmate-cardputer-zero")
        set(CPACK_PACKAGE_VENDOR "Trail Mate")
        set(CPACK_PACKAGE_CONTACT "Trail Mate Maintainers")
        set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
            "Trail Mate Cardputer Zero Linux shell")
        set(CPACK_PACKAGE_VERSION "${TRAIL_MATE_CARDPUTER_ZERO_PACKAGE_VERSION}")
        set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
        set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
        set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "arm64")
        set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Trail Mate Maintainers")
        set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
        set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
        set(CPACK_DEBIAN_PACKAGE_DEPENDS
            "libc6, libstdc++6, libgcc-s1, libsqlite3-0, libcurl4, libssl3, libarchive13, libjpeg62-turbo, libpng16-16, libsdl2-2.0-0, libwayland-client0, libxkbcommon0, libnotify-bin, gdal-bin, unzip, ca-certificates, network-manager")
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
        if(TRAIL_MATE_TMAP_PRODUCER_DIR)
            string(APPEND CPACK_DEBIAN_PACKAGE_DEPENDS ", libicu72")
        endif()
        set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
            "Trail Mate Cardputer Zero Wayland shell for the portable Linux device.")
        include(CPack)
    endif()
elseif(TRAIL_MATE_CARDPUTER_ZERO_BUILD_DEVICE)
    message(STATUS "Cardputer Zero device executables are enabled only on Linux hosts.")
endif()

if(TRAIL_MATE_CARDPUTER_ZERO_BUILD_SCREENSHOT_CAPTURE)
    add_executable(trailmate_linux_cardputer_zero_screenshot_capture
        tools/cardputer_zero_screenshot_capture.cpp)
    target_link_libraries(trailmate_linux_cardputer_zero_screenshot_capture
        PRIVATE trailmate_cardputer_zero_ui_shell)
    target_compile_features(trailmate_linux_cardputer_zero_screenshot_capture
        PRIVATE cxx_std_20)
endif()

if(BUILD_TESTING)
    add_executable(trailmate_cardputer_zero_wifi_profiles_test
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_lvgl/tests/linux_wifi_profiles_test.cpp")
    target_link_libraries(trailmate_cardputer_zero_wifi_profiles_test PRIVATE trailmate_cardputer_zero_linux_common)
    target_compile_options(trailmate_cardputer_zero_wifi_profiles_test PRIVATE -UNDEBUG)
    add_test(NAME trailmate_cardputer_zero_wifi_profiles COMMAND trailmate_cardputer_zero_wifi_profiles_test)
    set_tests_properties(trailmate_cardputer_zero_wifi_profiles PROPERTIES TIMEOUT 60)
    add_executable(trailmate_cardputer_zero_settings_strict_read_test
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_lvgl/tests/linux_settings_strict_read_test.cpp")
    target_link_libraries(trailmate_cardputer_zero_settings_strict_read_test PRIVATE trailmate_cardputer_zero_linux_common)
    target_compile_options(trailmate_cardputer_zero_settings_strict_read_test PRIVATE -UNDEBUG)
    add_test(NAME trailmate_cardputer_zero_settings_strict_read COMMAND trailmate_cardputer_zero_settings_strict_read_test)
    add_executable(trailmate_cardputer_zero_settings_reset_test
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_lvgl/tests/linux_settings_reset_test.cpp")
    target_link_libraries(trailmate_cardputer_zero_settings_reset_test PRIVATE trailmate_cardputer_zero_linux_common)
    target_compile_options(trailmate_cardputer_zero_settings_reset_test PRIVATE -UNDEBUG)
    add_test(NAME trailmate_cardputer_zero_settings_reset COMMAND trailmate_cardputer_zero_settings_reset_test)
    set_tests_properties(trailmate_cardputer_zero_settings_reset PROPERTIES TIMEOUT 60)
    add_executable(trailmate_cardputer_zero_reticulum_settings_test
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_lvgl/tests/linux_reticulum_network_settings_test.cpp")
    target_link_libraries(trailmate_cardputer_zero_reticulum_settings_test PRIVATE trailmate_cardputer_zero_ui_shell)
    target_compile_options(trailmate_cardputer_zero_reticulum_settings_test PRIVATE -UNDEBUG)
    add_test(NAME trailmate_cardputer_zero_reticulum_settings COMMAND trailmate_cardputer_zero_reticulum_settings_test)
    if(TARGET trailmate_cardputer_zero_linux_common AND TARGET OpenSSL::Crypto)
        add_executable(trailmate_cardputer_zero_reticulum_network_test
            "${TRAIL_MATE_REPO_ROOT}/apps/linux_lvgl/tests/linux_reticulum_network_routes_test.cpp")
        target_link_libraries(trailmate_cardputer_zero_reticulum_network_test PRIVATE trailmate_cardputer_zero_linux_common)
        target_compile_options(trailmate_cardputer_zero_reticulum_network_test PRIVATE -UNDEBUG)
        add_test(NAME trailmate_cardputer_zero_reticulum_network COMMAND trailmate_cardputer_zero_reticulum_network_test)
        set_tests_properties(trailmate_cardputer_zero_reticulum_network PROPERTIES TIMEOUT 30)
    endif()
    add_executable(trailmate_linux_lvgl_text_input_test tests/linux_lvgl_text_input_test.cpp)
    target_link_libraries(trailmate_linux_lvgl_text_input_test PRIVATE trailmate_cardputer_zero_ui_shell)
    target_compile_options(trailmate_linux_lvgl_text_input_test PRIVATE -UNDEBUG)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT CMAKE_CROSSCOMPILING)
        target_compile_options(trailmate_linux_lvgl_text_input_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(trailmate_linux_lvgl_text_input_test PRIVATE -fsanitize=address,undefined)
    endif()
    add_test(NAME trailmate_linux_lvgl_text_input COMMAND trailmate_linux_lvgl_text_input_test)
    add_executable(trailmate_linux_geocaching_page_test tests/linux_geocaching_page_test.cpp)
    add_executable(trailmate_cardputer_zero_page_help_test tests/linux_cardputer_zero_page_help_test.cpp)
    target_link_libraries(trailmate_cardputer_zero_page_help_test PRIVATE trailmate_cardputer_zero_ui_shell)
    target_compile_options(trailmate_cardputer_zero_page_help_test PRIVATE -UNDEBUG)
    foreach(page_index RANGE 0 12)
        add_test(NAME trailmate_cardputer_zero_page_help_${page_index}
                 COMMAND trailmate_cardputer_zero_page_help_test ${page_index})
    endforeach()
    add_executable(trailmate_linux_agenda_page_test tests/linux_agenda_page_test.cpp)
    target_link_libraries(trailmate_linux_agenda_page_test PRIVATE trailmate_cardputer_zero_ui_shell)
    target_compile_options(trailmate_linux_agenda_page_test PRIVATE -UNDEBUG)
    add_test(NAME trailmate_linux_agenda_page COMMAND trailmate_linux_agenda_page_test "${TRAIL_MATE_REPO_ROOT}/.tmp/agenda-card")
    target_link_libraries(trailmate_linux_geocaching_page_test PRIVATE trailmate_cardputer_zero_ui_shell)
    add_test(NAME trailmate_linux_geocaching_page COMMAND trailmate_linux_geocaching_page_test)
    add_executable(trailmate_linux_cardputer_zero_calculator_test
        tests/linux_cardputer_zero_calculator_test.cpp)
    target_link_libraries(trailmate_linux_cardputer_zero_calculator_test PRIVATE trailmate_cardputer_zero_ui_shell)
    add_test(NAME trailmate_linux_cardputer_zero_calculator COMMAND trailmate_linux_cardputer_zero_calculator_test)
    add_executable(trailmate_linux_cardputer_zero_calculator_engine_test
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_calculator_engine.cpp")
    target_link_libraries(trailmate_linux_cardputer_zero_calculator_engine_test PRIVATE trailmate_cardputer_zero_ui_shell)
    add_test(NAME trailmate_linux_cardputer_zero_calculator_engine COMMAND trailmate_linux_cardputer_zero_calculator_engine_test)
    if(TARGET trailmate_cardputer_zero_wayland)
        add_test(NAME trailmate_linux_cardputer_zero_launcher_syntax
                 COMMAND /bin/sh -n "${CMAKE_CURRENT_BINARY_DIR}/trailmate-cardputer-zero-applaunch")
    endif()
    add_executable(trailmate_linux_cardputer_zero_ux_pack_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/tests/test_cardputer_compact_ux_pack.cpp")
    target_link_libraries(trailmate_linux_cardputer_zero_ux_pack_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_linux_cardputer_zero_ux_pack_smoke
             COMMAND trailmate_linux_cardputer_zero_ux_pack_smoke)

    add_executable(trailmate_linux_cardputer_zero_app_shell_smoke
        tests/linux_cardputer_zero_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_cardputer_zero_app_shell_smoke
        PRIVATE trailmate_linux_cardputer_zero_shell)
    add_test(NAME trailmate_linux_cardputer_zero_app_shell_smoke
             COMMAND trailmate_linux_cardputer_zero_app_shell_smoke)

    add_executable(trailmate_linux_cardputer_zero_page_manifest_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_page_manifest.cpp")
    target_link_libraries(trailmate_linux_cardputer_zero_page_manifest_smoke
        PRIVATE trailmate_linux_cardputer_zero_shell)
    add_test(NAME trailmate_linux_cardputer_zero_page_manifest_smoke
             COMMAND trailmate_linux_cardputer_zero_page_manifest_smoke)

    add_executable(trailmate_linux_cardputer_zero_menu_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_cardputer_zero_menu_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/menu/menu_profile.cpp")
    target_include_directories(trailmate_linux_cardputer_zero_menu_profile_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/stubs"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_definitions(trailmate_linux_cardputer_zero_menu_profile_smoke
        PRIVATE TRAIL_MATE_CARDPUTER_ZERO_LINUX=1)
    target_compile_features(trailmate_linux_cardputer_zero_menu_profile_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_linux_cardputer_zero_menu_profile_smoke
             COMMAND trailmate_linux_cardputer_zero_menu_profile_smoke)

    add_executable(trailmate_linux_cardputer_zero_page_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_cardputer_zero_page_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/page/page_profile.cpp")
    target_include_directories(trailmate_linux_cardputer_zero_page_profile_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/stubs"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_definitions(trailmate_linux_cardputer_zero_page_profile_smoke
        PRIVATE TRAIL_MATE_CARDPUTER_ZERO_LINUX=1)
    target_compile_features(trailmate_linux_cardputer_zero_page_profile_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_linux_cardputer_zero_page_profile_smoke
             COMMAND trailmate_linux_cardputer_zero_page_profile_smoke)

    add_executable(trailmate_linux_cardputer_zero_business_wiring_smoke
        tests/linux_cardputer_zero_business_wiring_smoke.cpp)
    target_compile_features(trailmate_linux_cardputer_zero_business_wiring_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_linux_cardputer_zero_business_wiring_smoke
             COMMAND trailmate_linux_cardputer_zero_business_wiring_smoke
                     "${TRAIL_MATE_REPO_ROOT}")

    add_executable(trailmate_linux_cardputer_zero_packaging_smoke
        tests/linux_cardputer_zero_packaging_smoke.cpp)
    target_compile_features(trailmate_linux_cardputer_zero_packaging_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_linux_cardputer_zero_packaging_smoke
             COMMAND trailmate_linux_cardputer_zero_packaging_smoke
                     "${TRAIL_MATE_REPO_ROOT}")
endif()
