cmake_minimum_required(VERSION 3.16)
project(peer_store_recovery LANGUAGES CXX)
enable_testing()
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
add_executable(peer_recovery test_recovery.cpp
    ../team_position_store/fake_sd_runtime.cpp
    "${REPO}/platform/esp/arduino_common/src/chat/infra/store/sd_protocol_peer_repository.cpp"
    "${REPO}/platform/esp/arduino_common/src/chat/infra/store/fixed_slot_journal.cpp"
    "${REPO}/platform/esp/arduino_common/src/chat/infra/store/protocol_peer_codec.cpp"
    "${REPO}/platform/esp/arduino_common/src/chat/infra/store/protocol_chat_codec.cpp"
    "${REPO}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
    "${REPO}/modules/core_chat/src/infra/mesh_peer_directory_core.cpp")
target_compile_features(peer_recovery PRIVATE cxx_std_17)
target_compile_definitions(peer_recovery PRIVATE ARDUINO=1)
target_include_directories(peer_recovery PRIVATE stubs
    "${REPO}/platform/esp/arduino_common/include"
    "${REPO}/platform/esp/common/include"
    "${REPO}/modules/core_sys/include"
    "${REPO}/modules/core_chat/include")
target_compile_options(peer_recovery PRIVATE -Wall -Wextra -UNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer)
target_link_options(peer_recovery PRIVATE -fsanitize=address,undefined)
foreach(scenario IN ITEMS valid overwritten_header metadata_busy transient_open unknown_header interrupted_publish forensic_retry)
    add_test(NAME peer_recovery_${scenario} COMMAND peer_recovery ${scenario})
endforeach()
