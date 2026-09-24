cmake_minimum_required(VERSION 3.20)
project(TrailMateAgendaUiTests LANGUAGES C CXX)
include(CTest)
add_executable(agenda_countdown_test test_countdown.cpp)
target_include_directories(agenda_countdown_test PRIVATE ../../../ui_presentation/include)
target_compile_features(agenda_countdown_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(agenda_countdown_test PRIVATE /UNDEBUG)
else()
    target_compile_options(agenda_countdown_test PRIVATE -UNDEBUG)
endif()
add_test(NAME agenda_countdown COMMAND agenda_countdown_test)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME agenda_locale_coverage
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_locale_coverage.py")
add_test(NAME agenda_font_build_recipe
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/test_font_build_recipe.py")
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(LVGL_SOURCE_DIR "" CACHE PATH "LVGL 9.4 source directory")
if(NOT EXISTS "${LVGL_SOURCE_DIR}/lvgl.h")
    message(FATAL_ERROR "Set LVGL_SOURCE_DIR to the installed LVGL dependency")
endif()
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${LVGL_SOURCE_DIR}/src/*.c")
add_library(agenda_test_lvgl STATIC ${LVGL_SOURCES})
target_include_directories(agenda_test_lvgl PUBLIC "${LVGL_SOURCE_DIR}")
target_compile_definitions(agenda_test_lvgl PUBLIC
    LV_CONF_SKIP=1 LV_USE_STDLIB_MALLOC=1 LV_USE_STDLIB_STRING=1
    LV_USE_STDLIB_SPRINTF=1 LV_FONT_MONTSERRAT_12=1 LV_FONT_MONTSERRAT_16=1)
if(UNIX)
    target_link_libraries(agenda_test_lvgl PUBLIC m)
endif()
add_executable(agenda_font_binary_test test_font_binary.cpp)
target_link_libraries(agenda_font_binary_test PRIVATE agenda_test_lvgl)
target_compile_features(agenda_font_binary_test PRIVATE cxx_std_17)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_sources(agenda_font_binary_test PRIVATE font_memory_probe.cpp)
    target_compile_definitions(agenda_font_binary_test PRIVATE AGENDA_FONT_MEMORY_PROBE=1)
    target_link_options(agenda_font_binary_test PRIVATE
        "-Wl,--wrap=lv_malloc_core" "-Wl,--wrap=lv_realloc_core" "-Wl,--wrap=lv_free_core")
endif()
if(MSVC)
    target_compile_options(agenda_font_binary_test PRIVATE /UNDEBUG)
else()
    target_compile_options(agenda_font_binary_test PRIVATE -UNDEBUG)
endif()
# Font binaries are generated artifacts, not a dependency of ordinary UI tests.
# Opt in with explicit artifacts; never fetch npm packages during CTest.
set(AGENDA_FONT_BINARY "" CACHE FILEPATH "Generated locale font.bin to verify")
set(AGENDA_FONT_CHARSET "" CACHE FILEPATH "Corresponding UTF-8 charset.txt")
if(AGENDA_FONT_BINARY AND AGENDA_FONT_CHARSET)
    add_test(NAME agenda_font_binary COMMAND agenda_font_binary_test "${AGENDA_FONT_BINARY}" "${AGENDA_FONT_CHARSET}")
endif()
add_executable(agenda_ui_test
    map_preview_stub.cpp
    test_agenda_ui.cpp test_support.cpp popup_test_support.cpp test_map_flow.cpp test_font_scope.cpp test_interruption_flow.cpp test_touch_agenda.cpp
    "${REPO_ROOT}/apps/esp32_lvgl/tests/agenda/test_target_root.cpp"
    "${REPO_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_arduino_agenda.cpp"
    "${REPO_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_runtime_config.cpp"
    "${REPO_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${REPO_ROOT}/modules/product_composition/src/target_ux_binding.cpp"
    "${REPO_ROOT}/modules/ui_presentation/src/page/page_manifest.cpp"
    "${REPO_ROOT}/platform/esp/common/src/storage/agenda_file_store.cpp"
    "${REPO_ROOT}/platform/esp/common/src/storage/waypoint_file_store.cpp"
    "${REPO_ROOT}/modules/core_waypoint/src/waypoint.cpp"
    "${REPO_ROOT}/modules/core_waypoint/src/record_codec.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/assets/agenda.c"
    "${REPO_ROOT}/modules/core_agenda/src/event_codec.cpp"
    "${REPO_ROOT}/modules/core_agenda/src/agenda_service.cpp"
    "${REPO_ROOT}/modules/core_agenda/src/agenda_query.cpp"
    "${REPO_ROOT}/modules/core_agenda/src/recurrence.cpp"
    "${REPO_ROOT}/modules/core_agenda/src/reminder_scheduler.cpp"
    "${REPO_ROOT}/modules/ui_presentation/src/agenda/agenda_workspace_model.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_state.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_page_shell.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_page_flow.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_page_runtime.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_page_input.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_page_components.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_editor_runtime.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_editor_components.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/agenda/agenda_reminder_popup.cpp"
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/common/touch_text_editor.cpp"
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/src/common/input_layout.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/ime/ime_widget.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/ime/pinyin_ime.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/ime/ime_input_mode_descriptor.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/top_bar.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/page/page_profile.cpp")
target_include_directories(agenda_ui_test PRIVATE
    stubs
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REPO_ROOT}/apps/esp32_lvgl/tests/agenda/stubs"
    "${REPO_ROOT}/apps/esp32_lvgl/src"
    "${REPO_ROOT}/platform/esp/common/include"
    "${REPO_ROOT}/platform/esp/arduino_common/include"
    "${REPO_ROOT}/modules/core_agenda/include"
    "${REPO_ROOT}/modules/core_waypoint/include"
    "${REPO_ROOT}/modules/core_sys/include"
    "${REPO_ROOT}/modules/product_composition/include"
    "${REPO_ROOT}/modules/ui_presentation/include"
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/include"
    "${REPO_ROOT}/modules/ui_shared/include")
target_compile_features(agenda_ui_test PRIVATE cxx_std_17)
target_compile_definitions(agenda_ui_test PRIVATE UI_SHARED_TOUCH_IME_ENABLED=1)
set_source_files_properties(
    "${REPO_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_arduino_agenda.cpp"
    "${REPO_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_runtime_config.cpp"
    PROPERTIES COMPILE_DEFINITIONS ARDUINO_WIO_TRACKER_L2=1)
target_link_libraries(agenda_ui_test PRIVATE agenda_test_lvgl)
if(MSVC)
    target_compile_options(agenda_ui_test PRIVATE /UNDEBUG /utf-8)
else()
    target_compile_options(agenda_ui_test PRIVATE -UNDEBUG)
endif()
add_test(NAME agenda_ui COMMAND agenda_ui_test)

add_executable(agenda_catalog_test test_catalog.cpp test_support.cpp
    "${REPO_ROOT}/modules/ui_shared/src/ui/page/page_profile.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/app_catalog_builder.cpp")
target_include_directories(agenda_catalog_test PRIVATE catalog_stubs
    "${REPO_ROOT}/modules/ui_lvgl_ux_packs/include"
    "${REPO_ROOT}/modules/ui_shared/include")
target_compile_definitions(agenda_catalog_test PRIVATE ESP_PLATFORM=1 ARDUINO_T_LORA_PAGER=1)
target_compile_features(agenda_catalog_test PRIVATE cxx_std_17)
target_link_libraries(agenda_catalog_test PRIVATE agenda_test_lvgl)
if(MSVC)
    target_compile_options(agenda_catalog_test PRIVATE /UNDEBUG)
else()
    target_compile_options(agenda_catalog_test PRIVATE -UNDEBUG)
endif()
add_test(NAME agenda_catalog COMMAND agenda_catalog_test)

add_executable(agenda_text_snapshot_test test_text_snapshot.cpp)
target_include_directories(agenda_text_snapshot_test PRIVATE "${REPO_ROOT}/modules/ui_shared/include")
target_compile_features(agenda_text_snapshot_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(agenda_text_snapshot_test PRIVATE /UNDEBUG)
else()
    target_compile_options(agenda_text_snapshot_test PRIVATE -UNDEBUG)
endif()
add_test(NAME agenda_text_snapshot COMMAND agenda_text_snapshot_test)
