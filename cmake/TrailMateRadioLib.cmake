include_guard(GLOBAL)
include(FetchContent)

function(trailmate_add_radiolib target_name)
    if(TARGET ${target_name})
        return()
    endif()
    FetchContent_Declare(trailmate_linux_radio_source
        URL https://codeload.github.com/jgromes/RadioLib/tar.gz/2325a38a89dd0f0e402c19f67fe910111da1e246
        URL_HASH SHA256=23a074fd198086fedb1fd0b278e0815658a4e3ed497942e6a3e4fdfc95830ad9)
    FetchContent_GetProperties(trailmate_linux_radio_source)
    if(NOT trailmate_linux_radio_source_POPULATED)
        FetchContent_Populate(trailmate_linux_radio_source)
    endif()
    file(GLOB_RECURSE radio_sources CONFIGURE_DEPENDS
        "${trailmate_linux_radio_source_SOURCE_DIR}/src/*.cpp")
    list(FILTER radio_sources EXCLUDE REGEX "/hal/.*\\.cpp$")
    add_library(${target_name} STATIC ${radio_sources})
    target_include_directories(${target_name} PUBLIC "${trailmate_linux_radio_source_SOURCE_DIR}/src")
    target_compile_features(${target_name} PUBLIC cxx_std_17)
    set_property(TARGET ${target_name} PROPERTY TRAIL_MATE_LICENSE_FILE
        "${trailmate_linux_radio_source_SOURCE_DIR}/license.txt")
endfunction()
