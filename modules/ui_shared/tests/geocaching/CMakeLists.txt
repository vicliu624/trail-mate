cmake_minimum_required(VERSION 3.20)
project(GeocachingPageTests LANGUAGES C CXX)
enable_testing()
get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(LVGL_SOURCE_DIR "${REPO_ROOT}/.pio/libdeps/tlora_pager_lr1121/lvgl" CACHE PATH "Firmware LVGL source")
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${LVGL_SOURCE_DIR}/src/*.c")
add_library(page_test_lvgl STATIC ${LVGL_SOURCES})
target_include_directories(page_test_lvgl PUBLIC "${LVGL_SOURCE_DIR}")
target_compile_definitions(page_test_lvgl PUBLIC LV_CONF_SKIP=1 LV_USE_STDLIB_MALLOC=0
    LV_MEM_SIZE=1048576 LV_USE_STDLIB_STRING=1 LV_USE_STDLIB_SPRINTF=1
    LV_USE_SNAPSHOT=1 LV_FONT_MONTSERRAT_12=1 LV_FONT_MONTSERRAT_16=1)
add_executable(geocaching_page_test test_page.cpp
    "${REPO_ROOT}/modules/ui_shared/src/ui/screens/geocaching/geocaching_page_shell.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/components/two_pane_styles.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/widgets/top_bar.cpp"
    "${REPO_ROOT}/modules/ui_shared/src/ui/page/page_profile.cpp")
target_include_directories(geocaching_page_test PRIVATE stubs
    "${REPO_ROOT}/modules/ui_shared/include" "${REPO_ROOT}/modules/ui_presentation/include")
target_compile_features(geocaching_page_test PRIVATE cxx_std_17)
target_link_libraries(geocaching_page_test PRIVATE page_test_lvgl)
if(MSVC)
    target_compile_options(geocaching_page_test PRIVATE /utf-8)
endif()
add_test(NAME geocaching_page_480 COMMAND geocaching_page_test 480 222 "${CMAKE_CURRENT_BINARY_DIR}/pager")
add_test(NAME geocaching_page_320 COMMAND geocaching_page_test 320 240 "${CMAKE_CURRENT_BINARY_DIR}/tdeck")
