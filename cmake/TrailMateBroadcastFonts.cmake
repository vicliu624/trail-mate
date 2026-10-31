include_guard(GLOBAL)
include(GNUInstallDirs)

function(trailmate_add_broadcast_font_assets app_target)
    set(font_dir "${TRAIL_MATE_REPO_ROOT}/packs/ja-broadcast/fonts/ja-broadcast-cjk")
    set(TRAIL_MATE_ONESEG_BROADCAST_FONT_FILE "" CACHE FILEPATH
        "Prebuilt Japanese broadcast font; empty uses the source artifact or generates it in the build directory")
    option(TRAIL_MATE_ONESEG_GENERATE_BROADCAST_FONT
        "Generate a build-owned broadcast font even when a source artifact exists" OFF)
    if(TRAIL_MATE_ONESEG_BROADCAST_FONT_FILE)
        get_filename_component(font_file "${TRAIL_MATE_ONESEG_BROADCAST_FONT_FILE}" ABSOLUTE
            BASE_DIR "${TRAIL_MATE_REPO_ROOT}")
        if(NOT EXISTS "${font_file}")
            message(FATAL_ERROR "The supplied OneSeg broadcast font does not exist: ${font_file}")
        endif()
    elseif(EXISTS "${font_dir}/font.bin" AND NOT TRAIL_MATE_ONESEG_GENERATE_BROADCAST_FONT)
        set(font_file "${font_dir}/font.bin")
    else()
        find_package(Python3 REQUIRED COMPONENTS Interpreter)
        find_program(TRAIL_MATE_BROADCAST_NODE NAMES node nodejs REQUIRED)
        set(font_file "${CMAKE_CURRENT_BINARY_DIR}/broadcast-font/font.bin")
        add_custom_command(OUTPUT "${font_file}"
            COMMAND "${Python3_EXECUTABLE}" "${TRAIL_MATE_REPO_ROOT}/tools/generate_binfont_with_lv_font_conv.py"
                --font "${TRAIL_MATE_REPO_ROOT}/tools/fonts/NotoSansCJKjp-Regular.otf"
                --fallback-font "${TRAIL_MATE_REPO_ROOT}/tools/fonts/NotoSansMath-Regular.ttf"
                --charset-file "${font_dir}/charset.txt" --output "${font_file}"
                --size 16 --bpp 2 --no-compress --no-kerning
                --package-spec lv_font_conv@1.5.3 --node-exe "${TRAIL_MATE_BROADCAST_NODE}"
            DEPENDS "${font_dir}/charset.txt"
                "${TRAIL_MATE_REPO_ROOT}/tools/fonts/NotoSansCJKjp-Regular.otf"
                "${TRAIL_MATE_REPO_ROOT}/tools/fonts/NotoSansMath-Regular.ttf"
                "${TRAIL_MATE_REPO_ROOT}/tools/generate_binfont_with_lv_font_conv.py"
            COMMENT "Generating Japanese broadcast content font" VERBATIM)
    endif()
    add_custom_target(trailmate_oneseg_content_font DEPENDS "${font_file}")
    add_dependencies(${app_target} trailmate_oneseg_content_font)
    target_compile_definitions(${app_target} PRIVATE
        TRAIL_MATE_BUNDLED_FONT_ROOT="${CMAKE_INSTALL_FULL_DATADIR}/trailmate/packs")
    set(destination "${CMAKE_INSTALL_DATADIR}/trailmate/packs/fonts/ja-broadcast-cjk")
    install(FILES "${font_file}" DESTINATION "${destination}" RENAME font.bin COMPONENT Runtime)
    install(FILES "${font_dir}/manifest.ini" "${font_dir}/ranges.txt"
        "${font_dir}/LICENSE-Noto.txt" "${font_dir}/LICENSE-NotoMath.txt"
        "${font_dir}/font-source.json" "${font_dir}/math-font-source.json"
        DESTINATION "${destination}" COMPONENT Runtime)
    set(TRAIL_MATE_ONESEG_BROADCAST_FONT_EFFECTIVE "${font_file}" PARENT_SCOPE)
endfunction()
