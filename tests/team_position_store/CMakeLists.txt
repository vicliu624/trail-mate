cmake_minimum_required(VERSION 3.16)
project(team_position_store_contract LANGUAGES CXX)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_library(arduino_team_store_fixture STATIC
    fake_sd_runtime.cpp
    "${REPO}/platform/esp/arduino_common/src/ui/screens/team/team_ui_store.cpp"
    "${REPO}/modules/ui_shared/src/ui/team_persistence/team_ui_snapshot_codec.cpp")
target_compile_features(arduino_team_store_fixture PUBLIC cxx_std_17)
target_include_directories(arduino_team_store_fixture PUBLIC
    "${REPO}/platform/esp/arduino_common/include"
    "${REPO}/platform/esp/common/include"
    "${REPO}/modules/core_sys/include"
    "${REPO}/modules/core_team/include"
    "${REPO}/modules/core_chat/include"
    "${REPO}/modules/core_gps/include"
    "${REPO}/modules/ui_shared/include"
    "${REPO}/modules/ui_map_runtime/include"
    "${REPO}/modules/ui_presentation/include")
target_compile_options(arduino_team_store_fixture PRIVATE -UNDEBUG -ffunction-sections -fdata-sections)
target_link_options(arduino_team_store_fixture INTERFACE -Wl,--gc-sections)

add_executable(arduino_team_position_store_contract test_store.cpp)
target_link_libraries(arduino_team_position_store_contract PRIVATE arduino_team_store_fixture)
target_compile_options(arduino_team_position_store_contract PRIVATE -UNDEBUG)
add_test(NAME arduino_team_position_store_contract COMMAND arduino_team_position_store_contract)

add_executable(arduino_team_position_map_bridge
    test_map_bridge.cpp
    "${REPO}/modules/ui_shared/src/ui/presentation_sources/team_map_overlay_source.cpp")
target_link_libraries(arduino_team_position_map_bridge PRIVATE arduino_team_store_fixture)
target_compile_options(arduino_team_position_map_bridge PRIVATE -UNDEBUG)
add_test(NAME arduino_team_position_map_bridge COMMAND arduino_team_position_map_bridge)
