# Device-owned installation and Debian metadata. The generic Linux shell does
# not know the device name, desktop launcher, or OS distribution dependencies.
if(CMAKE_CROSSCOMPILING AND NOT TRAIL_MATE_TARGET_CXX_ABI_ROOT)
    message(FATAL_ERROR "CM0 cross-packaging requires its target-distribution C++ ABI overlay; generic Debian armhf libstdc++ is incompatible with Raspbian")
endif()
if(NOT TRAIL_MATE_LINUX_UX_PACK STREQUAL "t_display_p4_touch")
    message(FATAL_ERROR "T-Display-CM0 requires the t_display_p4_touch UX pack")
endif()
set(CPACK_PACKAGE_NAME trailmate-cm0)
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Trail Mate touch application for T-Display-CM0")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION
    "Trail Mate fullscreen Linux application using the shared T-Display-P4 touch UX.")
set(TRAIL_MATE_DEVICE_DEBIAN_DEPENDS
    "libc6 (>= 2.38), libstdc++6 (>= 14), libgcc-s1, libsdl2-2.0-0 (>= 2.0.18), libcurl4t64, libarchive13t64, libjpeg62-turbo, libpng16-16t64, libsqlite3-0, libssl3t64, ca-certificates, network-manager, systemd, init-system-helpers (>= 1.54~), kmod, python3, wlopm")
if(TRAIL_MATE_TMAP_PRODUCER_DIR)
    string(APPEND TRAIL_MATE_DEVICE_DEBIAN_DEPENDS ", libicu76")
endif()
if(TRAIL_MATE_LINUX_ENABLE_ONESEG_MEDIA)
    if(NOT TARGET trailmate_linux_oneseg_media)
        message(FATAL_ERROR "CM0 1seg package requires the native television media target")
    endif()
    trailmate_oneseg_debian_dependencies(oneseg_dependencies)
    string(APPEND TRAIL_MATE_DEVICE_DEBIAN_DEPENDS ", ${oneseg_dependencies}")
endif()
install(PROGRAMS "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/tools/register_i2c_peripheral.py"
    DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}/trailmate" RENAME register-i2c-peripheral COMPONENT Runtime)
configure_file("${CMAKE_CURRENT_LIST_DIR}/trailmate-cm0-rtc.service.in"
    "${CMAKE_CURRENT_BINARY_DIR}/trailmate-cm0-rtc.service" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/trailmate-cm0-rtc.service"
    DESTINATION "lib/systemd/system" PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION TRUE)
set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA "")
foreach(control_script IN ITEMS postinst prerm postrm)
    # Extensionless scripts can otherwise retain Windows checkout line endings.
    configure_file("${CMAKE_CURRENT_LIST_DIR}/packaging/${control_script}"
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/${control_script}" @ONLY NEWLINE_STYLE UNIX)
    list(APPEND CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
        "${CMAKE_CURRENT_BINARY_DIR}/packaging/${control_script}")
endforeach()
if(TARGET trailmate_linux_radiolib)
    get_target_property(radio_license trailmate_linux_radiolib TRAIL_MATE_LICENSE_FILE)
    install(FILES "${radio_license}" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0"
        RENAME RadioLib-license.txt PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
endif()
find_program(TRAIL_MATE_GZIP gzip REQUIRED)
configure_file("${CMAKE_CURRENT_LIST_DIR}/changelog.in"
    "${CMAKE_CURRENT_BINARY_DIR}/changelog" @ONLY)
execute_process(COMMAND "${TRAIL_MATE_GZIP}" -n -9 -c "${CMAKE_CURRENT_BINARY_DIR}/changelog"
    OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz" COMMAND_ERROR_IS_FATAL ANY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/changelog.gz"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0"
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/kernel/tests/test_panel_backlight.py"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0/kernel/tests"
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/device.conf"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/trailmate/devices" RENAME t-display-cm0.conf
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${TRAIL_MATE_REPO_ROOT}/docs/engineering/linux-rtc-integration.md"
    "${TRAIL_MATE_REPO_ROOT}/docs/engineering/linux-output-power.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0"
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/kernel/0001-rm69a10-backlight.patch"
    "${CMAKE_CURRENT_LIST_DIR}/kernel/README.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0/kernel"
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
configure_file("${CMAKE_CURRENT_LIST_DIR}/trailmate-cm0.desktop.in"
    "${CMAKE_CURRENT_BINARY_DIR}/trailmate-cm0.desktop" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/trailmate-cm0.desktop"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${TRAIL_MATE_REPO_ROOT}/apps/linux_lvgl/README.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0"
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${TRAIL_MATE_REPO_ROOT}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0" RENAME copyright
    PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${TRAIL_MATE_REPO_ROOT}/modules/core_sstv/LICENSE-DSP"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0"
    RENAME SSTV-DSP-license.txt PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${TRAIL_MATE_REPO_ROOT}/third_party/codec2/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0"
    RENAME Codec2-license.txt PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
install(FILES "${TRAIL_MATE_REPO_ROOT}/third_party/rweather_crypto/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/trailmate-cm0"
    RENAME rweather-Crypto-license.txt PERMISSIONS ${TRAIL_MATE_DATA_PERMISSIONS} COMPONENT Runtime)
