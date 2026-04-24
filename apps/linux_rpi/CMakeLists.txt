cmake_minimum_required(VERSION 3.24)

project(TrailMateCardputerZeroRpi VERSION 0.1.0 LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "The Cardputer Zero Pi OS device shell must be configured on Linux.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT "${PROJECT_SOURCE_DIR}/../../platform/linux/common/src")
set(TRAIL_MATE_CARDPUTER_ZERO_RPI_ROOT "${PROJECT_SOURCE_DIR}/../../platform/linux/rpi/src")

add_library(trailmate_cardputer_zero_linux_common
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/app/demo_app.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/app/demo_app_runner.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/core/bitmap_font.cpp"
    "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}/core/canvas.cpp"
)

target_include_directories(trailmate_cardputer_zero_linux_common
    PUBLIC
        "${TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT}"
)
target_compile_features(trailmate_cardputer_zero_linux_common PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(trailmate_cardputer_zero_linux_common PRIVATE /W4 /permissive- /FS)
else()
    target_compile_options(trailmate_cardputer_zero_linux_common PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(trailmate_cardputer_zero_device
    "${TRAIL_MATE_CARDPUTER_ZERO_RPI_ROOT}/platform/device/linux_framebuffer_platform.cpp"
    src/targets/device_main.cpp
)
target_include_directories(trailmate_cardputer_zero_device
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
        "${TRAIL_MATE_CARDPUTER_ZERO_RPI_ROOT}"
)
target_link_libraries(trailmate_cardputer_zero_device PRIVATE trailmate_cardputer_zero_linux_common)
