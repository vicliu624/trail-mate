cmake_minimum_required(VERSION 3.16)
project(gps_ubx_contract LANGUAGES CXX)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(TINYGPSPLUS_SOURCE_DIR "${REPO}/.pio/libdeps/tlora_pager_sx1262/TinyGPSPlus" CACHE PATH "TinyGPSPlus 1.0.3 source directory")
if(NOT EXISTS "${TINYGPSPLUS_SOURCE_DIR}/src/TinyGPS++.cpp")
    include(FetchContent)
    FetchContent_Declare(tinygpsplus
        URL https://codeload.github.com/mikalhart/TinyGPSPlus/zip/81ba32cc9ac63a8e4c20ae624ac48513766c88fc
        URL_HASH SHA256=ff152dac284510f34f34f733d1901159a46336076d351cf7869de956df5a46ac)
    FetchContent_MakeAvailable(tinygpsplus)
    set(TINYGPSPLUS_SOURCE_DIR "${tinygpsplus_SOURCE_DIR}")
endif()
add_library(gps_fixture STATIC
    "${REPO}/platform/esp/arduino_common/src/gps/GPS.cpp"
    "${TINYGPSPLUS_SOURCE_DIR}/src/TinyGPS++.cpp")
target_compile_features(gps_fixture PUBLIC cxx_std_17)
target_compile_definitions(gps_fixture PUBLIC ARDUINO=10812)
target_include_directories(gps_fixture PUBLIC
    stubs
    "${REPO}/platform/esp/arduino_common/include"
    "${REPO}/modules/core_gps/include"
    "${TINYGPSPLUS_SOURCE_DIR}/src")
target_compile_options(gps_fixture PUBLIC -UNDEBUG)
add_executable(gps_ubx_contract test_ubx.cpp)
target_link_libraries(gps_ubx_contract PRIVATE gps_fixture)
foreach(scenario ack fragmented checksum unrelated nmea continue)
    add_test(NAME gps_ubx_${scenario} COMMAND gps_ubx_contract ${scenario})
endforeach()
add_executable(gps_version_contract test_version.cpp)
target_link_libraries(gps_version_contract PRIVATE gps_fixture)
add_test(NAME gps_ubx_version COMMAND gps_version_contract)
add_executable(gps_m10_contract test_m10.cpp)
target_link_libraries(gps_m10_contract PRIVATE gps_fixture)
foreach(scenario power systems full compact off nak)
    add_test(NAME gps_m10_${scenario} COMMAND gps_m10_contract ${scenario})
endforeach()
