include_guard(GLOBAL)
include(FetchContent)

function(trailmate_add_linux_radiolib)
    if(TARGET trailmate_linux_radiolib)
        return()
    endif()
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The spidev/gpiochip RadioLib backend requires Linux")
    endif()
    FetchContent_Declare(trailmate_linux_radio_source
        URL https://codeload.github.com/jgromes/RadioLib/tar.gz/2325a38a89dd0f0e402c19f67fe910111da1e246
        URL_HASH SHA256=23a074fd198086fedb1fd0b278e0815658a4e3ed497942e6a3e4fdfc95830ad9)
    FetchContent_GetProperties(trailmate_linux_radio_source)
    if(NOT trailmate_linux_radio_source_POPULATED)
        FetchContent_Populate(trailmate_linux_radio_source)
    endif()
    # Isolated target: no changes to the ESP dependency pin or upstream install
    # rules, and no machine-specific Linux HAL from the vendor sample.
    file(GLOB_RECURSE radio_sources CONFIGURE_DEPENDS
        "${trailmate_linux_radio_source_SOURCE_DIR}/src/*.cpp")
    list(FILTER radio_sources EXCLUDE REGEX "/hal/.*\\.cpp$")
    add_library(trailmate_linux_radiolib STATIC ${radio_sources})
    target_include_directories(trailmate_linux_radiolib PUBLIC
        "${trailmate_linux_radio_source_SOURCE_DIR}/src")
    target_compile_features(trailmate_linux_radiolib PUBLIC cxx_std_20)
    set_property(TARGET trailmate_linux_radiolib PROPERTY TRAIL_MATE_LICENSE_FILE
        "${trailmate_linux_radio_source_SOURCE_DIR}/license.txt")
endfunction()

function(trailmate_add_linux_lr2021 target_name)
    if(TARGET ${target_name})
        return()
    endif()
    trailmate_add_linux_radiolib()
    add_library(${target_name} STATIC
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/linux_radiolib_hal.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/lr2021_radio.cpp")
    target_include_directories(${target_name} PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_link_libraries(${target_name} PRIVATE trailmate_linux_radiolib)
    target_compile_features(${target_name} PUBLIC cxx_std_20)
endfunction()
