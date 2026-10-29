include_guard(GLOBAL)
function(trailmate_add_linux_gnss)
    if(TARGET trailmate_linux_gnss_source)
        return()
    endif()
    find_package(Threads REQUIRED)
    add_library(trailmate_linux_gnss_source
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/gpio_power.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/gnss_source.cpp")
    target_include_directories(trailmate_linux_gnss_source PUBLIC
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
    target_compile_features(trailmate_linux_gnss_source PUBLIC cxx_std_17)
    target_link_libraries(trailmate_linux_gnss_source PUBLIC Threads::Threads)
endfunction()
