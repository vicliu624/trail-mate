cmake_minimum_required(VERSION 3.24)

project(TrailMateCardputerZeroSimulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
find_package(Threads REQUIRED)

set(TRAIL_MATE_CARDPUTER_ZERO_COMMON_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../platform/linux/common/include")
set(TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT "${PROJECT_SOURCE_DIR}/../../platform/linux/common/src")
set(TRAIL_MATE_CORE_SYS_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_sys/include")
set(TRAIL_MATE_CORE_CHAT_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_chat/include")
set(TRAIL_MATE_CORE_CHAT_GENERATED_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_chat/generated")
set(TRAIL_MATE_CORE_CHAT_NANOPB_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_chat/third_party/nanopb")
set(TRAIL_MATE_CORE_TEAM_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_team/include")
set(TRAIL_MATE_CORE_GPS_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_gps/include")
set(TRAIL_MATE_CORE_HOSTLINK_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_hostlink/include")
set(TRAIL_MATE_UI_SHARED_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/ui_shared/include")
set(TRAIL_MATE_UI_SHARED_SRC_ROOT "${PROJECT_SOURCE_DIR}/../../modules/ui_shared/src")
set(TRAIL_MATE_PLATFORM_SHARED_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../platform/shared/include")

function(trailmate_apply_common_warnings target_name)
    if(MSVC)
        target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /FS)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(trailmate_cardputer_zero_linux_common
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/app/linux_app_facade.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/app/demo_app.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/app/demo_app_runner.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/core/bitmap_font.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/core/canvas.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/device_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/firmware_update_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/gps_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/hostlink_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/lora_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/orientation_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/pack_repository_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/route_storage.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/screen_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/settings_store.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/sstv_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/team_ui_store_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/time_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/tracker_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/usb_support_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/walkie_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/wifi_runtime.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/domain/chat_model.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/infra/contact_store_core.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/infra/mesh_protocol_utils.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/infra/node_store_blob_format.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/infra/node_store_core.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/infra/store/ram_store.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/usecase/chat_service.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/usecase/contact_service.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_chat.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_location_marker.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_mgmt.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_pairing_wire.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_position.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_track.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_waypoint.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/protocol/team_wire.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/usecase/team_controller.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/usecase/team_pairing_coordinator.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/usecase/team_service.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_team/src/usecase/team_track_sampler.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_hostlink/src/hostlink_session.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_sys/src/app/app_facade_access.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_sys/src/sys/clock.cpp"
)

target_include_directories(trailmate_cardputer_zero_linux_common
    PUBLIC
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_INCLUDE_ROOT}"
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}"
        "${TRAIL_MATE_CORE_CHAT_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_CHAT_GENERATED_ROOT}"
        "${TRAIL_MATE_CORE_CHAT_NANOPB_ROOT}"
        "${TRAIL_MATE_CORE_GPS_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_HOSTLINK_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_SYS_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_TEAM_INCLUDE_ROOT}"
        "${TRAIL_MATE_UI_SHARED_INCLUDE_ROOT}"
)
target_compile_features(trailmate_cardputer_zero_linux_common PUBLIC cxx_std_20)
target_compile_definitions(trailmate_cardputer_zero_linux_common PUBLIC TRAIL_MATE_LORA_TX_POWER_MAX_DBM=22)
target_link_libraries(trailmate_cardputer_zero_linux_common PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(trailmate_cardputer_zero_linux_common PUBLIC ws2_32)
endif()
trailmate_apply_common_warnings(trailmate_cardputer_zero_linux_common)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(SDL3)

add_executable(trailmate_cardputer_zero_simulator
    src/platform/simulator/sdl_simulator.cpp
    src/targets/simulator_main.cpp
)
target_include_directories(trailmate_cardputer_zero_simulator PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(trailmate_cardputer_zero_simulator PRIVATE trailmate_cardputer_zero_linux_common SDL3::SDL3)

set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/../../platform/linux/common/include/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.4.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(lvgl)

add_library(trailmate_cardputer_zero_ui_shell
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/localization.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/shared_ui_shell.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/shell_ui_runner.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/ui_common.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/ui_status.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/gps_shared_compat.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/mt_protocol_air_compat.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/widgets/map/map_tiles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/app_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Chat.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/alert.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/AreaCleared.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/BaseCamp.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/contact.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/ext.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/gps.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/GoodFind.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/img_usb.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/logo.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/rally.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/rf.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Satellite.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Setting.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Spectrum.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/sstv.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/sos.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/team.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/tracker.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/walkie_talkie.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/air_status_footer.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/fonts/font_utils.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/info_card.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_nav.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/components/two_pane_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/formatters.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/dashboard/dashboard_style.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/page/page_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/common/placeholder_page.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_compose_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_conversation_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_message_list_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_protocol_support.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_send_flow.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/chat/chat_ui_controller.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_page_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/contacts/contacts_state.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/energy_sweep/energy_sweep_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/extensions/extensions_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/extensions/extensions_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gnss/gnss_skyplot_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gnss/gnss_skyplot_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gps/gps_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/gps/gps_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/node_info/node_info_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/node_info/node_info_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/pc_link/pc_link_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/pc_link/pc_link_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_page_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/settings/settings_state.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/sstv/sstv_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/sstv/sstv_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_page_styles.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/team/team_state.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_components.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_input.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/tracker/tracker_state.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/walkie_talkie/walkie_talkie_page_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/screens/walkie_talkie/walkie_talkie_page_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/startup_ui_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/ui_boot.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/ime/ime_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/busy_overlay.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/map/map_viewport.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/system_notification.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/toast/toast_widget.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/widgets/top_bar.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/infra/meshcore/mc_region_presets.cpp"
    "${PROJECT_SOURCE_DIR}/../../modules/core_chat/src/infra/meshtastic/mt_region.cpp"
)
target_include_directories(trailmate_cardputer_zero_ui_shell
    PUBLIC
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_INCLUDE_ROOT}"
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}"
        "${TRAIL_MATE_CORE_CHAT_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_CHAT_GENERATED_ROOT}"
        "${TRAIL_MATE_CORE_CHAT_NANOPB_ROOT}"
        "${TRAIL_MATE_CORE_TEAM_INCLUDE_ROOT}"
        "${TRAIL_MATE_UI_SHARED_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_SYS_INCLUDE_ROOT}"
        "${TRAIL_MATE_PLATFORM_SHARED_INCLUDE_ROOT}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)
target_compile_features(trailmate_cardputer_zero_ui_shell PUBLIC cxx_std_20)
target_compile_definitions(
    trailmate_cardputer_zero_ui_shell
    PRIVATE
        TRAIL_MATE_BOOT_MIN_SHOW_MS=1200
        TRAIL_MATE_CARDPUTER_ZERO_LINUX=1
        GAT562_NO_PINYIN_IME=1
)
target_link_libraries(trailmate_cardputer_zero_ui_shell
    PUBLIC
        lvgl
        trailmate_cardputer_zero_linux_common
)
trailmate_apply_common_warnings(trailmate_cardputer_zero_ui_shell)
target_link_libraries(trailmate_cardputer_zero_simulator PRIVATE trailmate_cardputer_zero_ui_shell)

if(BUILD_TESTING)
    add_executable(trailmate_cardputer_zero_smoke tests/demo_app_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_smoke PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_smoke COMMAND trailmate_cardputer_zero_smoke)

    add_executable(trailmate_cardputer_zero_runtime_smoke tests/linux_runtime_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_runtime_smoke PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_runtime_smoke COMMAND trailmate_cardputer_zero_runtime_smoke)
endif()
