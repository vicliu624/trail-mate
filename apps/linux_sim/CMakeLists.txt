cmake_minimum_required(VERSION 3.24)

project(TrailMateCardputerZeroSimulator VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(TRAIL_MATE_CARDPUTER_ZERO_COMMON_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../platform/linux/common/include")
set(TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT "${PROJECT_SOURCE_DIR}/../../platform/linux/common/src")
set(TRAIL_MATE_CORE_SYS_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_sys/include")
set(TRAIL_MATE_CORE_GPS_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_gps/include")
set(TRAIL_MATE_CORE_HOSTLINK_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/core_hostlink/include")
set(TRAIL_MATE_UI_SHARED_INCLUDE_ROOT "${PROJECT_SOURCE_DIR}/../../modules/ui_shared/include")
set(TRAIL_MATE_UI_SHARED_SRC_ROOT "${PROJECT_SOURCE_DIR}/../../modules/ui_shared/src")

function(trailmate_apply_common_warnings target_name)
    if(MSVC)
        target_compile_definitions(${target_name} PRIVATE _CRT_SECURE_NO_WARNINGS)
        target_compile_options(${target_name} PRIVATE /W4 /permissive- /FS)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(trailmate_cardputer_zero_linux_common
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
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/route_storage.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/screen_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/settings_store.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/sstv_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/time_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/tracker_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/usb_support_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/walkie_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/platform/ui/wifi_runtime.cpp"
)

target_include_directories(trailmate_cardputer_zero_linux_common
    PUBLIC
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_INCLUDE_ROOT}"
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}"
        "${TRAIL_MATE_CORE_GPS_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_HOSTLINK_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_SYS_INCLUDE_ROOT}"
)
target_compile_features(trailmate_cardputer_zero_linux_common PUBLIC cxx_std_20)
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

set(LV_BUILD_CONF_PATH "${PROJECT_SOURCE_DIR}/lv_conf.h" CACHE PATH "" FORCE)
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
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/menu_runtime.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/ui_common.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/ui/ui_status.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/app_runtime.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Chat.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/ext.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/gps.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/logo.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/Setting.c"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/assets/fonts/font_utils.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/formatters.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_layout.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/menu/menu_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/page/page_profile.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/startup_ui_shell.cpp"
    "${TRAIL_MATE_UI_SHARED_SRC_ROOT}/ui/ui_boot.cpp"
    src/ui_shell/shell_ui_runner.cpp
    src/ui_shell/shared_ui_shell.cpp
)
target_include_directories(trailmate_cardputer_zero_ui_shell
    PUBLIC
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_INCLUDE_ROOT}"
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}"
        "${TRAIL_MATE_UI_SHARED_INCLUDE_ROOT}"
        "${TRAIL_MATE_CORE_SYS_INCLUDE_ROOT}"
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)
target_compile_features(trailmate_cardputer_zero_ui_shell PUBLIC cxx_std_20)
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
