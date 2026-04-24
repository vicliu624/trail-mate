cmake_minimum_required(VERSION 3.24)

project(TrailMateCardputerZeroSimulator VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

set(TRAIL_MATE_CARDPUTER_ZERO_COMMON_ROOT "${PROJECT_SOURCE_DIR}/../../platform/linux/common/src")

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

if(BUILD_TESTING)
    add_executable(trailmate_cardputer_zero_smoke tests/demo_app_smoke.cpp)
    target_link_libraries(trailmate_cardputer_zero_smoke PRIVATE trailmate_cardputer_zero_linux_common)
    add_test(NAME trailmate_cardputer_zero_smoke COMMAND trailmate_cardputer_zero_smoke)
endif()
