cmake_minimum_required(VERSION 3.16)
project(sd_transport_tests LANGUAGES CXX)
enable_testing()
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(STORAGE "${REPO}/platform/esp/arduino_common")
add_executable(sdmmc_transfer_test
    test_sdmmc_transfer.cpp
    "${STORAGE}/src/storage/sdmmc_block_device.cpp")
target_include_directories(sdmmc_transfer_test PRIVATE
    "${REPO}/third_party/sdfat/src" stubs "${STORAGE}/include")
target_compile_definitions(sdmmc_transfer_test PRIVATE TRAIL_MATE_SDFAT_SDMMC=1
    ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0 ENABLE_ARDUINO_STRING=0)
add_test(NAME sdmmc_transfer COMMAND sdmmc_transfer_test)
foreach(transport IN ITEMS SHARED_SPI SDMMC)
    add_executable(sd_policy_${transport} test_sd_policy.cpp)
    target_include_directories(sd_policy_${transport} PRIVATE
        "${STORAGE}/include" "${REPO}/modules/core_sys/include")
    target_compile_definitions(sd_policy_${transport} PRIVATE TRAIL_MATE_SDFAT_${transport}=1)
    add_test(NAME sd_policy_${transport} COMMAND sd_policy_${transport})
endforeach()

# Exercise the real FAT/exFAT implementation and the production SDMMC block
# device together, replacing only the hardware with a sparse in-memory card.
file(GLOB SDFAT_SOURCES
    "${REPO}/third_party/sdfat/src/common/*.cpp"
    "${REPO}/third_party/sdfat/src/FatLib/*.cpp"
    "${REPO}/third_party/sdfat/src/ExFatLib/*.cpp"
    "${REPO}/third_party/sdfat/src/FsLib/*.cpp")
add_executable(sd_filesystem_test test_sd_filesystem.cpp ${SDFAT_SOURCES}
    "${STORAGE}/src/storage/sdmmc_block_device.cpp")
target_include_directories(sd_filesystem_test PRIVATE
    "${REPO}/third_party/sdfat/src" stubs "${STORAGE}/include")
target_compile_definitions(sd_filesystem_test PRIVATE
    TRAIL_MATE_SDFAT_SDMMC=1 USE_BLOCK_DEVICE_INTERFACE=1 SDFAT_FILE_TYPE=3
    ENABLE_ARDUINO_FEATURES=0 ENABLE_ARDUINO_SERIAL=0 ENABLE_ARDUINO_STRING=0)
target_compile_options(sd_filesystem_test PRIVATE
    -include "${CMAKE_CURRENT_SOURCE_DIR}/stubs/host_clock.h")
add_test(NAME sd_filesystem COMMAND sd_filesystem_test)
