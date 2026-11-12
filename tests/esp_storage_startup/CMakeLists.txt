cmake_minimum_required(VERSION 3.16)
project(esp_storage_startup LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
enable_testing()
add_executable(storage_startup
    test_startup.cpp
    "${REPO}/platform/esp/arduino_common/src/app_runtime_bootstrap_support.cpp")
target_include_directories(storage_startup PRIVATE
    stubs
    "${REPO}/platform/esp/arduino_common/include"
    "${REPO}/platform/esp/common/include"
    "${REPO}/platform/esp/boards/include"
    "${REPO}/modules/core_sys/include")
target_compile_options(storage_startup PRIVATE -Wall -Wextra -UNDEBUG)
add_test(NAME storage_startup COMMAND storage_startup)
