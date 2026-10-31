include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/TrailMateRadioLib.cmake")

function(trailmate_add_linux_radiolib)
    if(TARGET trailmate_linux_radiolib)
        return()
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The spidev/gpiochip RadioLib backend requires Linux")
    endif()
    trailmate_add_radiolib(trailmate_linux_radiolib)
endfunction()

function(trailmate_add_linux_lr2021 target_name)
    if(TARGET ${target_name})
        return()
    endif()
    trailmate_add_linux_radiolib()
    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/TrailMateLr2021Iq.cmake")
    trailmate_add_lr2021_iq(trailmate_linux_lr2021_iq)
    add_library(${target_name} STATIC
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/linux_radiolib_hal.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/lr2021_radio.cpp")
    target_include_directories(${target_name} PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_include_directories(${target_name} PRIVATE
        "${TRAIL_MATE_REPO_ROOT}/platform/shared/include")
    target_link_libraries(${target_name} PRIVATE trailmate_linux_radiolib trailmate_linux_lr2021_iq)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
endfunction()
