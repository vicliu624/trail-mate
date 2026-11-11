cmake_minimum_required(VERSION 3.16)
project(team_position_store_contract LANGUAGES CXX)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(arduino_team_position_store_contract
    test_store.cpp
    fake_sd_runtime.cpp
    "${REPO}/platform/esp/arduino_common/src/ui/screens/team/team_ui_store.cpp"
    "${REPO}/modules/ui_shared/src/ui/team_persistence/team_ui_snapshot_codec.cpp")
target_compile_features(arduino_team_position_store_contract PRIVATE cxx_std_17)
target_include_directories(arduino_team_position_store_contract PRIVATE
    "${REPO}/platform/esp/arduino_common/include"
    "${REPO}/platform/esp/common/include"
    "${REPO}/modules/core_sys/include"
    "${REPO}/modules/core_team/include"
    "${REPO}/modules/core_chat/include"
    "${REPO}/modules/core_gps/include"
    "${REPO}/modules/ui_shared/include")
target_compile_options(arduino_team_position_store_contract PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
target_link_options(arduino_team_position_store_contract PRIVATE -Wl,--gc-sections)
add_test(NAME arduino_team_position_store_contract COMMAND arduino_team_position_store_contract)
