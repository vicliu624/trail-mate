cmake_minimum_required(VERSION 3.20)
project(music_core_tests LANGUAGES C CXX)
find_package(Threads REQUIRED)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(LVGL_DIR "" CACHE PATH "Firmware LVGL source")
add_executable(music_tags test_tags.cpp
 "${REPO}/modules/core_music/src/id3.cpp"
 "${REPO}/modules/music_image/src/cover_decoder.cpp"
 image_memory.cpp
 "${LVGL_DIR}/src/libs/tjpgd/tjpgd.c"
 "${LVGL_DIR}/src/libs/lodepng/lodepng.c")
target_include_directories(music_tags PRIVATE . "${LVGL_DIR}" "${REPO}/modules/core_music/include" "${REPO}/modules/music_image/include" "${REPO}/modules/core_sys/include")
target_compile_definitions(music_tags PRIVATE LV_CONF_INCLUDE_SIMPLE LODEPNG_NO_COMPILE_DISK LODEPNG_NO_COMPILE_ENCODER)
target_compile_options(music_tags PRIVATE -UNDEBUG -Wall -Wextra -fstack-usage)
enable_testing()
add_executable(es8311_gain test_es8311_gain.cpp)
target_include_directories(es8311_gain PRIVATE "${REPO}/platform/esp/wio_tracker_l2/include")
target_compile_options(es8311_gain PRIVATE -UNDEBUG)
add_test(NAME es8311_gain COMMAND es8311_gain)
add_executable(media_volume test_media_volume.cpp "${REPO}/modules/core_sys/src/platform/audio/media_volume.cpp")
target_include_directories(media_volume PRIVATE "${REPO}/modules/core_sys/include")
target_compile_options(media_volume PRIVATE -UNDEBUG)
add_test(NAME media_volume COMMAND media_volume)
add_executable(radio_power test_radio_power.cpp "${REPO}/modules/core_sys/src/platform/radio/radio_power.cpp")
target_include_directories(radio_power PRIVATE "${REPO}/modules/core_sys/include")
target_compile_options(radio_power PRIVATE -UNDEBUG)
add_test(NAME radio_power COMMAND radio_power)
add_executable(device_controls test_controls.cpp "${REPO}/platform/esp/common/src/quick_controls_runtime.cpp" "${REPO}/modules/core_sys/src/platform/radio/radio_power.cpp")
target_include_directories(device_controls PRIVATE controls_fakes "${REPO}/modules/core_sys/include" "${REPO}/modules/core_music/include" "${REPO}/modules/core_gps/include" "${REPO}/modules/ui_presentation/include" "${REPO}/platform/esp/boards/include" "${REPO}/platform/esp/idf_common/include")
target_compile_definitions(device_controls PRIVATE TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1)
target_compile_options(device_controls PRIVATE -UNDEBUG)
add_test(NAME device_controls COMMAND device_controls)
add_executable(device_controls_lr2021 test_controls.cpp "${REPO}/platform/esp/common/src/quick_controls_runtime.cpp" "${REPO}/modules/core_sys/src/platform/radio/radio_power.cpp")
target_include_directories(device_controls_lr2021 PRIVATE controls_fakes "${REPO}/modules/core_sys/include" "${REPO}/modules/core_music/include" "${REPO}/modules/core_gps/include" "${REPO}/modules/ui_presentation/include" "${REPO}/platform/esp/boards/include" "${REPO}/platform/esp/idf_common/include")
target_compile_definitions(device_controls_lr2021 PRIVATE TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1 TRAIL_MATE_IDF_RADIO_LR2021=1)
target_compile_options(device_controls_lr2021 PRIVATE -UNDEBUG)
add_test(NAME device_controls_lr2021 COMMAND device_controls_lr2021)
add_test(NAME music_tags COMMAND music_tags "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
add_executable(music_focus test_focus.cpp "${REPO}/modules/core_sys/src/platform/audio/audio_focus.cpp")
target_include_directories(music_focus PRIVATE "${REPO}/modules/core_sys/include")
target_compile_options(music_focus PRIVATE -UNDEBUG)
add_test(NAME music_focus COMMAND music_focus)
add_executable(music_decoder test_decoder.cpp "${REPO}/modules/core_music/src/decoder.cpp" "${REPO}/modules/core_music/src/id3.cpp")
target_include_directories(music_decoder PRIVATE "${REPO}/modules/core_music/include")
target_compile_options(music_decoder PRIVATE -UNDEBUG -O2 -fstack-usage)
add_test(NAME music_decoder COMMAND music_decoder "${REPO}/docs/sound/call.mp3" "${REPO}/docs/sound/pager-notification.mp3")
add_executable(music_playback_policy test_playback_policy.cpp)
target_include_directories(music_playback_policy PRIVATE "${REPO}/modules/core_music/include")
target_compile_options(music_playback_policy PRIVATE -UNDEBUG)
add_test(NAME music_playback_policy COMMAND music_playback_policy)
add_executable(music_lyrics test_lyrics.cpp "${REPO}/modules/core_music/src/lyrics.cpp" "${REPO}/modules/core_music/src/id3.cpp")
target_include_directories(music_lyrics PRIVATE "${REPO}/modules/core_music/include")
target_compile_options(music_lyrics PRIVATE -UNDEBUG -O2 -fstack-usage)
add_test(NAME music_lyrics COMMAND music_lyrics)
add_executable(music_catalog test_catalog.cpp "${REPO}/modules/core_music/src/catalog.cpp" "${REPO}/modules/core_music/src/storage.cpp" "${REPO}/modules/core_music/src/id3.cpp")
target_include_directories(music_catalog PRIVATE "${REPO}/modules/core_music/include")
target_compile_options(music_catalog PRIVATE -UNDEBUG -O2 -fstack-usage)
add_test(NAME music_catalog COMMAND music_catalog)
add_executable(music_playlists test_playlists.cpp "${REPO}/modules/core_music/src/playlists.cpp" "${REPO}/modules/core_music/src/catalog.cpp" "${REPO}/modules/core_music/src/storage.cpp" "${REPO}/modules/core_music/src/id3.cpp")
target_include_directories(music_playlists PRIVATE "${REPO}/modules/core_music/include")
target_compile_options(music_playlists PRIVATE -UNDEBUG -O2 -fstack-usage)
add_test(NAME music_playlists COMMAND music_playlists "${REPO}/docs/sound/call.mp3")
add_executable(music_player test_player.cpp
 "${REPO}/modules/core_music/src/player.cpp" "${REPO}/modules/core_music/src/playlists.cpp"
 "${REPO}/modules/core_music/src/catalog.cpp" "${REPO}/modules/core_music/src/storage.cpp"
 "${REPO}/modules/core_music/src/decoder.cpp" "${REPO}/modules/core_music/src/lyrics.cpp" "${REPO}/modules/core_music/src/id3.cpp")
target_include_directories(music_player PRIVATE "${REPO}/modules/core_music/include")
target_compile_options(music_player PRIVATE -UNDEBUG -O2 -fstack-usage)
add_test(NAME music_player COMMAND music_player "${REPO}/docs/sound/call.mp3")
foreach(TARGET music_tags music_focus music_player)
    target_sources(${TARGET} PRIVATE "${REPO}/platform/linux/common/src/platform/resource_lock.cpp")
endforeach()
foreach(TARGET music_decoder music_playback_policy music_lyrics music_catalog music_playlists music_player)
    target_include_directories(${TARGET} PRIVATE "${REPO}/modules/core_sys/include")
endforeach()
add_executable(music_runtime test_runtime.cpp
    "${REPO}/platform/esp/common/src/music/music_runtime.cpp"
    "${REPO}/platform/esp/common/src/music/music_output.cpp"
    "${REPO}/modules/core_music/src/player.cpp" "${REPO}/modules/core_music/src/playlists.cpp"
    "${REPO}/modules/core_music/src/catalog.cpp" "${REPO}/modules/core_music/src/storage.cpp"
    "${REPO}/modules/core_music/src/decoder.cpp" "${REPO}/modules/core_music/src/lyrics.cpp"
    "${REPO}/modules/core_music/src/id3.cpp"
    "${REPO}/modules/core_sys/src/platform/audio/audio_focus.cpp"
    "${REPO}/modules/core_sys/src/platform/audio/media_volume.cpp"
    "${REPO}/platform/linux/common/src/platform/resource_lock.cpp")
target_include_directories(music_runtime PRIVATE runtime_fakes .
    "${REPO}/platform/esp/common/include"
    "${REPO}/modules/core_music/include" "${REPO}/modules/music_image/include"
    "${REPO}/modules/core_sys/include")
target_compile_definitions(music_runtime PRIVATE ESP_PLATFORM=1 TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4=1)
target_compile_options(music_runtime PRIVATE -UNDEBUG -O2)
add_test(NAME music_runtime COMMAND music_runtime "${REPO}/docs/sound/call.mp3")
get_target_property(MUSIC_RUNTIME_SOURCES music_runtime SOURCES)
add_executable(music_runtime_l2 ${MUSIC_RUNTIME_SOURCES})
target_include_directories(music_runtime_l2 PRIVATE runtime_fakes .
    "${REPO}/platform/esp/common/include"
    "${REPO}/modules/core_music/include" "${REPO}/modules/music_image/include"
    "${REPO}/modules/core_sys/include")
target_compile_definitions(music_runtime_l2 PRIVATE ESP_PLATFORM=1 ARDUINO_WIO_TRACKER_L2=1)
target_compile_options(music_runtime_l2 PRIVATE -UNDEBUG -O2)
add_test(NAME music_runtime_l2 COMMAND music_runtime_l2 "${REPO}/docs/sound/call.mp3")
add_executable(music_output test_output.cpp
    "${REPO}/platform/esp/common/src/music/music_output.cpp"
    "${REPO}/modules/core_sys/src/platform/audio/audio_focus.cpp"
    "${REPO}/platform/linux/common/src/platform/resource_lock.cpp")
target_include_directories(music_output PRIVATE runtime_fakes .
    "${REPO}/modules/core_music/include" "${REPO}/modules/core_sys/include"
    "${REPO}/platform/esp/common/include")
target_compile_definitions(music_output PRIVATE ESP_PLATFORM=1 ARDUINO_WIO_TRACKER_L2=1)
target_compile_options(music_output PRIVATE -UNDEBUG -O2)
target_link_libraries(music_output PRIVATE Threads::Threads)
add_test(NAME music_output COMMAND music_output)
get_target_property(MUSIC_PLAYER_SOURCES music_player SOURCES)
list(FILTER MUSIC_PLAYER_SOURCES EXCLUDE REGEX "(test_player|decoder)\\.cpp$")
add_executable(music_rate_transition test_rate_transition.cpp ${MUSIC_PLAYER_SOURCES})
target_include_directories(music_rate_transition PRIVATE .
    "${REPO}/modules/core_music/include" "${REPO}/modules/core_sys/include")
target_compile_options(music_rate_transition PRIVATE -UNDEBUG -O2)
add_test(NAME music_rate_transition COMMAND music_rate_transition "${REPO}/docs/sound/call.mp3")
add_executable(music_pager_audio test_pager_audio.cpp
    "${REPO}/platform/esp/common/src/music/music_audio.cpp")
target_include_directories(music_pager_audio PRIVATE audio_fakes runtime_fakes .
    "${REPO}/modules/core_music/include" "${REPO}/modules/core_sys/include"
    "${REPO}/platform/esp/common/include")
target_compile_definitions(music_pager_audio PRIVATE ARDUINO_T_LORA_PAGER=1)
target_compile_options(music_pager_audio PRIVATE -UNDEBUG)
add_test(NAME music_pager_audio COMMAND music_pager_audio)
