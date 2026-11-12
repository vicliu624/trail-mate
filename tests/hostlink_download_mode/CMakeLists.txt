cmake_minimum_required(VERSION 3.16)
project(hostlink_download_mode LANGUAGES CXX)
enable_testing()
add_executable(download_mode test_download_mode.cpp)
target_compile_features(download_mode PRIVATE cxx_std_17)
target_compile_options(download_mode PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_include_directories(download_mode PRIVATE ../../modules/core_hostlink/include)
add_test(NAME download_mode COMMAND download_mode)

foreach(target IN ITEMS arduino_s3 arduino_unsupported)
    add_executable(${target} test_arduino_binding.cpp
        ../../platform/esp/arduino_common/src/hostlink/hostlink_download_mode.cpp)
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -UNDEBUG)
    target_include_directories(${target} PRIVATE stubs ../../modules/core_hostlink/include
        ../../platform/esp/arduino_common/include)
    add_test(NAME ${target} COMMAND ${target})
endforeach()
target_compile_definitions(arduino_s3 PRIVATE CONFIG_IDF_TARGET_ESP32S3=1)

add_executable(idf_unsupported test_idf_unsupported.cpp)
target_compile_features(idf_unsupported PRIVATE cxx_std_17)
target_compile_options(idf_unsupported PRIVATE -Wall -Wextra -Werror -UNDEBUG)
target_compile_definitions(idf_unsupported PRIVATE ESP_PLATFORM=1 CONFIG_IDF_TARGET_ESP32P4=1)
target_include_directories(idf_unsupported PRIVATE ../../modules/core_hostlink/include
    ../../platform/esp/arduino_common/include)
add_test(NAME idf_unsupported COMMAND idf_unsupported)
