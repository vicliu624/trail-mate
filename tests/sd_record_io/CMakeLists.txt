cmake_minimum_required(VERSION 3.20)
project(SdRecordIoSessionTests LANGUAGES CXX)
enable_testing()
add_executable(sd_record_io_test test_session.cpp
    ../../platform/esp/arduino_common/src/storage/sd_record_file_io.cpp)
target_include_directories(sd_record_io_test PRIVATE stubs
    ../../platform/esp/arduino_common/include ../../platform/esp/common/include)
target_compile_features(sd_record_io_test PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(sd_record_io_test PRIVATE /UNDEBUG)
else()
    target_compile_options(sd_record_io_test PRIVATE -UNDEBUG)
endif()
add_test(NAME sd_record_io_session COMMAND sd_record_io_test)
