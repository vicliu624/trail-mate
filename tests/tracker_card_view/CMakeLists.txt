cmake_minimum_required(VERSION 3.20)
project(tracker_card_view_contract LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(LVGL_DIR "" CACHE PATH "LVGL source")
set(LVGL_LIBRARY "" CACHE FILEPATH "Compatible prebuilt LVGL")
set(REPO "${CMAKE_CURRENT_LIST_DIR}/../..")
if(NOT LVGL_LIBRARY)
    set(LV_BUILD_CONF_PATH "${REPO}/tests/contacts_card_view/lv_conf.h" CACHE PATH "" FORCE)
    set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
    set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory(${LVGL_DIR} lvgl)
    set(LVGL_LIBRARY lvgl)
endif()
add_executable(tracker_card_view_contract test_view.cpp ${REPO}/third_party/yxml/yxml.c)
target_include_directories(tracker_card_view_contract PRIVATE
    ${REPO}/modules/ui_shared/include ${REPO}/modules/core_sys/include
    ${REPO}/modules/core_track_sharing/include ${REPO}/modules/core_gps/include
    ${REPO}/third_party/yxml ${LVGL_DIR})
target_compile_definitions(tracker_card_view_contract PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(tracker_card_view_contract PRIVATE ${REPO}/tests/contacts_card_view)
target_link_libraries(tracker_card_view_contract PRIVATE ${LVGL_LIBRARY})
enable_testing()
add_test(NAME tracker_card_view_contract COMMAND tracker_card_view_contract)
add_executable(bottom_bar_contract test_bottom_bar.cpp
    ${REPO}/modules/ui_shared/src/ui/components/bottom_bar.cpp
    ${REPO}/modules/ui_shared/src/ui/components/two_pane_nav.cpp)
target_include_directories(bottom_bar_contract PRIVATE ${REPO}/modules/ui_shared/include ${LVGL_DIR} ${REPO}/tests/contacts_card_view)
target_compile_definitions(bottom_bar_contract PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(bottom_bar_contract PRIVATE ${LVGL_LIBRARY})
add_test(NAME bottom_bar_contract COMMAND bottom_bar_contract)

foreach(dialog_target dialog_frame_contract dialog_frame_cardputer_contract)
add_executable(${dialog_target} test_dialog_frame.cpp
    ${REPO}/modules/ui_shared/src/ui/components/dialog_frame.cpp
    ${REPO}/modules/ui_shared/src/ui/components/bottom_bar.cpp
    ${REPO}/modules/ui_shared/src/ui/widgets/top_bar.cpp)
target_include_directories(${dialog_target} PRIVATE ${REPO}/modules/ui_shared/include ${LVGL_DIR} ${REPO}/tests/contacts_card_view)
target_compile_definitions(${dialog_target} PRIVATE LV_CONF_INCLUDE_SIMPLE)
if(dialog_target STREQUAL "dialog_frame_cardputer_contract")
    target_compile_definitions(${dialog_target} PRIVATE TRAIL_MATE_CARDPUTER_ZERO_LINUX=1)
endif()
target_link_libraries(${dialog_target} PRIVATE ${LVGL_LIBRARY})
add_test(NAME ${dialog_target} COMMAND ${dialog_target})
endforeach()

add_executable(team_viewport_contract test_team_viewport.cpp
    ${REPO}/modules/ui_shared/src/ui/screens/team/team_page_layout.cpp
    ${REPO}/modules/ui_shared/src/ui/screens/team/team_page_input.cpp)
target_include_directories(team_viewport_contract PRIVATE
    ${REPO}/modules/ui_shared/include ${REPO}/platform/linux/common/include
    ${LVGL_DIR} ${REPO}/tests/contacts_card_view)
target_compile_definitions(team_viewport_contract PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_link_libraries(team_viewport_contract PRIVATE ${LVGL_LIBRARY})
add_test(NAME team_viewport_contract COMMAND team_viewport_contract)

add_executable(team_runtime_clock_contract test_team_runtime_clock.cpp
    ${REPO}/platform/esp/arduino_common/src/team/runtime/team_runtime_arduino.cpp
    ${REPO}/modules/core_sys/src/sys/clock.cpp
    ${REPO}/modules/ui_shared/src/ui/screens/team/team_page_read_model.cpp
    ${REPO}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp)
target_include_directories(team_runtime_clock_contract PRIVATE
    stubs ${REPO}/modules/ui_shared/include ${REPO}/modules/core_sys/include
    ${REPO}/modules/core_team/include ${REPO}/platform/esp/arduino_common/include)
add_test(NAME team_runtime_clock_contract COMMAND team_runtime_clock_contract)
foreach(team_contract team_viewport_contract team_runtime_clock_contract)
    target_compile_options(${team_contract} PRIVATE -UNDEBUG)
endforeach()

add_executable(contacts_compact_card_contract ${REPO}/tests/contacts_card_view/test_view.cpp)
target_include_directories(contacts_compact_card_contract PRIVATE
    ${REPO}/modules/ui_shared/include ${LVGL_DIR} ${REPO}/tests/contacts_card_view)
target_compile_definitions(contacts_compact_card_contract PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_compile_options(contacts_compact_card_contract PRIVATE -UNDEBUG)
target_link_libraries(contacts_compact_card_contract PRIVATE ${LVGL_LIBRARY})
add_test(NAME contacts_compact_card_contract COMMAND contacts_compact_card_contract)
