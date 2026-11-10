include_guard(GLOBAL)
include(GNUInstallDirs)

function(trailmate_enable_linux_fonts shell_target)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()
    find_package(Freetype REQUIRED)
    target_link_libraries(lvgl PUBLIC Freetype::Freetype)
    target_compile_definitions(lvgl PUBLIC TRAIL_MATE_LINUX_FREETYPE=1)
    target_compile_definitions(${shell_target} PUBLIC
        TRAIL_MATE_BUNDLED_FONT_ROOT="${CMAKE_INSTALL_FULL_DATADIR}/trailmate/packs")
endfunction()

function(trailmate_add_linux_font_validation test_target shell_target)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_CROSSCOMPILING)
        return()
    endif()
    if(NOT test_target MATCHES "^[A-Za-z0-9_]+$")
        message(FATAL_ERROR "Font validation requires a plain target name")
    endif()
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(font_python "$ENV{TRAIL_MATE_FONT_TOOLS_PYTHON}")
    if(NOT font_python)
        set(font_python "${Python3_EXECUTABLE}")
    endif()
    # This constant child of the current build directory owns generated assets.
    set(stage "${CMAKE_CURRENT_BINARY_DIR}/${test_target}-assets")
    set(pack_root "${stage}/usr/share/trailmate/packs")
    file(GLOB_RECURSE locale_inputs CONFIGURE_DEPENDS
        "${TRAIL_MATE_REPO_ROOT}/packs/*/locales/*/manifest.ini"
        "${TRAIL_MATE_REPO_ROOT}/packs/*/locales/*/strings.tsv")
    add_custom_command(OUTPUT "${pack_root}/linux-font-pack.json"
        COMMAND "${CMAKE_COMMAND}" -E rm -rf "${stage}"
        COMMAND "${font_python}" "${TRAIL_MATE_REPO_ROOT}/tools/build_linux_font_pack.py"
            --stage "${stage}" --version "${TRAIL_MATE_DEBIAN_PROJECT_VERSION}"
        DEPENDS "${TRAIL_MATE_REPO_ROOT}/tools/build_linux_font_pack.py"
            "${TRAIL_MATE_REPO_ROOT}/tools/fonts/NotoSansCJKsc-Regular.otf"
            "${TRAIL_MATE_REPO_ROOT}/tools/fonts/NotoSansCJKjp-Regular.otf"
            "${TRAIL_MATE_REPO_ROOT}/tools/fonts/NotoSans-Regular.ttf"
            "${TRAIL_MATE_REPO_ROOT}/tools/fonts/linux/TrailMateColorEmoji14.ttf"
            "${TRAIL_MATE_REPO_ROOT}/tools/fonts/linux/font-source.json"
            ${locale_inputs}
        COMMENT "Preparing independent Linux font/language validation assets" VERBATIM)
    add_custom_target(${test_target}_assets DEPENDS "${pack_root}/linux-font-pack.json")
    add_executable(${test_target}
        "${TRAIL_MATE_REPO_ROOT}/apps/linux_cardputer_zero/tests/linux_rich_font_test.cpp")
    target_link_libraries(${test_target} PRIVATE ${shell_target})
    target_compile_options(${test_target} PRIVATE -UNDEBUG)
    add_dependencies(${test_target} ${test_target}_assets)
    add_test(NAME ${test_target} COMMAND ${test_target}
        "${pack_root}" "${CMAKE_CURRENT_BINARY_DIR}/linux-rich-fonts.png")
    add_test(NAME ${test_target}_user_override COMMAND ${test_target}
        "${pack_root}" "${CMAKE_CURRENT_BINARY_DIR}/linux-rich-fonts-override.png" --user-override)
    add_test(NAME ${test_target}_corrupt_font COMMAND ${test_target}
        "${pack_root}" "${CMAKE_CURRENT_BINARY_DIR}/linux-rich-fonts-corrupt.png" --corrupt-font)
    set_tests_properties(${test_target} ${test_target}_user_override ${test_target}_corrupt_font PROPERTIES TIMEOUT 120)
endfunction()
