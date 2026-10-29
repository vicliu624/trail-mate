include_guard(GLOBAL)

function(trailmate_enable_linux_media target_name)
    include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateLinuxCodec2.cmake")
    trailmate_add_linux_codec2()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT TARGET lvgl)
        message(FATAL_ERROR "Linux media requires the Linux platform and shared LVGL image decoder")
    endif()
    find_package(SDL2 2.0.18 REQUIRED)
    target_sources(${target_name} PRIVATE
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/music_storage.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/music_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/sdl_pcm_output.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/sdl_pcm_input.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/walkie_session.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/voice_audio.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/call_audio_session.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/voice_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/vmp_session.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/voice_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/voice_contacts.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_walkie/src/packet.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sstv/src/cordic.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sstv/src/decode_sstv.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sstv/src/half_band_filter2.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sstv/src/receiver.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/src/storage.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/src/catalog.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/src/playlists.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/src/id3.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/src/lyrics.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/src/decoder.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/src/player.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/music_image/src/cover_decoder.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/audio/media_volume.cpp")
    target_include_directories(${target_name} PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/modules/core_music/include"
        "${TRAIL_MATE_REPO_ROOT}/modules/music_image/include")
    target_include_directories(${target_name} PRIVATE
        "${TRAIL_MATE_REPO_ROOT}/modules/core_walkie/include"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sstv/include")
    target_link_libraries(${target_name} PRIVATE SDL2::SDL2 lvgl trailmate_linux_codec2)
    target_compile_definitions(${target_name} PRIVATE TRAIL_MATE_LINUX_HAS_MEDIA=1)
endfunction()
