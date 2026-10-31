include_guard(GLOBAL)
include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/TrailMateLr2021IqOverlay.cmake)

# RX-only portable engine. Board ownership, bounded HAL transactions and
# measured FIFO service latency remain the responsibility of each adapter.
function(trailmate_add_lr2021_iq target_name)
    if(TARGET ${target_name})
        return()
    endif()
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(trailmate_lr2021_iq_source
        URL https://codeload.github.com/SAMS0N1TE/lr2021-sdr/tar.gz/0a44fc69beb34274d6e86f29c1c064b3d720c48d
        URL_HASH SHA256=117b39cae34b0785ab9fbd9c161fe83ca6632155f5180cf82bfc025c91ae60ec
        SOURCE_SUBDIR trailmate-receive-only)
    # The non-project subdirectory populates sources without adding upstream's
    # host servers, TX tools, global CTest settings or optional dependencies.
    FetchContent_MakeAvailable(trailmate_lr2021_iq_source)
    set(rx_overlay "${CMAKE_CURRENT_BINARY_DIR}/${target_name}-rx-overlay")
    trailmate_prepare_lr2021_iq_overlay("${trailmate_lr2021_iq_source_SOURCE_DIR}" "${rx_overlay}")
    add_library(${target_name} STATIC
        "${rx_overlay}/lr2021_iq.c"
        "${trailmate_lr2021_iq_source_SOURCE_DIR}/src/lr2021_iq_queue.c")
    target_include_directories(${target_name} PUBLIC
        "${rx_overlay}/include"
        "${trailmate_lr2021_iq_source_SOURCE_DIR}/include")
    target_compile_features(${target_name} PRIVATE c_std_11)
    set_property(TARGET ${target_name} PROPERTY TRAIL_MATE_LICENSE_FILE
        "${trailmate_lr2021_iq_source_SOURCE_DIR}/LICENSE")
    set_property(TARGET ${target_name} PROPERTY TRAIL_MATE_SOURCE_DIR
        "${trailmate_lr2021_iq_source_SOURCE_DIR}")
endfunction()
